/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/*
** SDL3GameEngine.cpp
**
** Linux implementation of GameEngine using SDL3 for windowing/input.
**
** TheSuperHackers @feature CnC_Generals_Linux 07/02/2026
** Provides SDL3-based input and window management for Linux builds.
** Based on fighter19 reference implementation.
*/

#ifndef _WIN32

#include "SDL3GameEngine.h"
#include "OpenALAudioManager.h"
#include "SDL3Device/GameClient/SDL3Mouse.h"
#include "SDL3Device/GameClient/SDL3Keyboard.h"
#include "GameClient/Mouse.h"
#include "GameClient/Keyboard.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/Gadget.h"
#include "W3DDevice/GameLogic/W3DGameLogic.h"
#include "W3DDevice/GameClient/W3DGameClient.h"
#include "W3DDevice/Common/W3DModuleFactory.h"
#include "W3DDevice/Common/W3DThingFactory.h"
#include "W3DDevice/Common/W3DFunctionLexicon.h"
#include "W3DDevice/Common/W3DRadar.h"
#include "W3DDevice/GameClient/W3DParticleSys.h"
#include "W3DDevice/GameClient/W3DWebBrowser.h"
#include "StdDevice/Common/StdLocalFileSystem.h"
#include "StdDevice/Common/StdBIGFileSystem.h"
#include "Common/GlobalData.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

// ZH Mobile: the touch translator, lifecycle gate and touch-mouse dedup below were
// written for iOS but use only SDL3 APIs, so Android shares them verbatim.
#if (defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE) || defined(__ANDROID__)
#define GX_TOUCH_PLATFORM 1
#else
#define GX_TOUCH_PLATFORM 0
#endif

// Extern globals for input devices (set by GameClient)
extern Mouse *TheMouse;
extern Keyboard *TheKeyboard;
extern GameWindowManager *TheWindowManager;

#if GX_TOUCH_PLATFORM
#include <atomic>

// ---------------------------------------------------------------------------
// iOS app lifecycle
//
// iOS suspends the process when the app leaves the foreground. Any GPU work
// submitted around suspension stalls on drawable acquisition (MoltenVK waits
// out a timeout per present), which surfaces as multi-second input hangs right
// after resuming. SDL warns that lifecycle events can arrive outside the
// normal poll cycle, so they are captured in an event watcher that fires
// immediately on the delivering thread; the engine update loop checks the
// flag and skips simulation + rendering while backgrounded.
// ---------------------------------------------------------------------------
// Two independent reasons to halt the render/sim loop on iOS:
//  - BACKGROUNDED (home / switched away): the process is about to be suspended.
//  - INACTIVE (multitasking switcher open, Control Center, a notification
//    banner): iOS snapshots the window and owns the CAMetalLayer drawable during
//    this window — and crucially, opening the app switcher fires resign-active
//    WITHOUT a full background transition.
// Acquiring a Metal drawable during EITHER state fights iOS for the layer; across
// repeated suspend/switcher cycles MoltenVK is driven into an unrecoverable
// surface state and the app crashes (the reported "crashes after backgrounding /
// multitasking a few times"). Pause whenever either is set.
static std::atomic<bool> s_appBackgrounded{false};
static std::atomic<bool> s_appInactive{false};

static inline bool iosShouldPauseRendering()
{
	return s_appBackgrounded.load() || s_appInactive.load();
}

static bool SDLCALL iosLifecycleWatcher(void *userdata, SDL_Event *event)
{
#if defined(__ANDROID__)
	// Lifecycle breadcrumbs: suspend/resume problems are otherwise invisible in logs.
	switch (event->type) {
		case SDL_EVENT_WILL_ENTER_BACKGROUND: fprintf(stderr, "INFO: lifecycle: will enter background\n"); break;
		case SDL_EVENT_DID_ENTER_BACKGROUND:  fprintf(stderr, "INFO: lifecycle: did enter background\n"); break;
		case SDL_EVENT_WILL_ENTER_FOREGROUND: fprintf(stderr, "INFO: lifecycle: will enter foreground\n"); break;
		case SDL_EVENT_DID_ENTER_FOREGROUND:  fprintf(stderr, "INFO: lifecycle: did enter foreground\n"); break;
		case SDL_EVENT_WINDOW_FOCUS_LOST:     fprintf(stderr, "INFO: lifecycle: focus lost\n"); break;
		case SDL_EVENT_WINDOW_FOCUS_GAINED:   fprintf(stderr, "INFO: lifecycle: focus gained\n"); break;
		case SDL_EVENT_LOW_MEMORY:            fprintf(stderr, "WARNING: lifecycle: low memory\n"); break;
		case SDL_EVENT_TERMINATING:           fprintf(stderr, "INFO: lifecycle: terminating\n"); break;
		default: break;
	}
#endif
	switch (event->type) {
		case SDL_EVENT_WILL_ENTER_BACKGROUND:
		case SDL_EVENT_DID_ENTER_BACKGROUND:
			s_appBackgrounded.store(true);
			break;
		case SDL_EVENT_DID_ENTER_FOREGROUND:
			s_appBackgrounded.store(false);
			break;
		// Resign/become active. On iOS, SDL maps applicationWillResignActive ->
		// window focus lost and applicationDidBecomeActive -> window focus gained.
		// Stay paused until fully active again (focus regained), which arrives
		// after DID_ENTER_FOREGROUND.
		case SDL_EVENT_WINDOW_FOCUS_LOST:
			s_appInactive.store(true);
			break;
		case SDL_EVENT_WINDOW_FOCUS_GAINED:
			s_appInactive.store(false);
			break;
		default:
			break;
	}
	return true;
}

// ---------------------------------------------------------------------------
// iOS touch -> mouse gesture translation
//
// SDL's automatic touch-mouse synthesis is disabled on iOS (SDL3Main.cpp sets
// SDL_HINT_TOUCH_MOUSE_EVENTS=0); every mouse event the game sees on iOS is
// synthesized here, through the same SDL3Mouse::addSDLEvent path real mice use.
//
// Gestures (matching the game's stock control scheme, which is LMB-centric):
//   1 finger tap/drag     -> left button click / drag (select, command, drag-box)
//   1 finger long-press   -> right button click (deselect), if finger stays put
//   2 finger drag         -> right-button drag at the centroid (camera scroll)
//   2 finger pinch        -> mouse wheel (camera zoom)
// ---------------------------------------------------------------------------
namespace {

struct TouchState {
	enum Phase {
		IDLE,        // no fingers tracked
		PENDING,     // finger1 down, gesture identity not yet known, nothing sent
		DRAGGING,    // finger1 drag in progress, LMB held
		LONGPRESSED, // long-press fired (RMB click sent); a later drag still starts a box
		TWO_PENDING, // two fingers down, pan vs. pinch not decided yet, nothing sent
		PAN,         // two-finger camera pan, RMB held
		ZOOM         // two-finger pinch, wheel steps only
	};

	Phase phase = IDLE;
	SDL_FingerID finger1 = 0;
	SDL_FingerID finger2 = 0;
	float downX = 0.0f, downY = 0.0f;   // finger1 down position (window units)
	float lastX = 0.0f, lastY = 0.0f;   // finger1 latest position
	float panX = 0.0f, panY = 0.0f;     // pan centroid
	float startCX = 0.0f, startCY = 0.0f; // centroid when the second finger landed
	float startDist = 0.0f;             // finger distance when the second finger landed
	float pinchDist = 0.0f;             // finger distance at last wheel step
	Uint64 downTicks = 0;
	float f1x = 0.0f, f1y = 0.0f, f2x = 0.0f, f2y = 0.0f; // normalized per finger

	// A tap's button-up is delivered one frame after its button-down. Parts of the
	// 2003 UI (notably the radar/minimap) poll button state once per frame, and a
	// down+up pair inside one frame never shows up there as "pressed".
	int pendingUpFrames = 0;
	Uint8 pendingUpButton = 0;
	float pendingUpX = 0.0f, pendingUpY = 0.0f;
};

TouchState s_touch;

const Uint64 LONG_PRESS_MS = 700;
const float PINCH_STEP_RATIO = 0.06f;  // 6% distance change per wheel tick
const float PINCH_DECIDE_RATIO = 0.10f;  // 10% distance change before a pan moved => zoom
const float TAP_DEAD_ZONE_PX = 8.0f;   // jitter below this keeps a tap a tap

// Window units differ per platform (iOS: points, Android: physical pixels), so the
// dead zone is also floored at ~1.2% of the short screen edge.
inline float tapDeadZone(int winW, int winH)
{
	const float shortEdge = (float)(winW < winH ? winW : winH);
	const float relative = shortEdge * 0.012f;
	return relative > TAP_DEAD_ZONE_PX ? relative : TAP_DEAD_ZONE_PX;
}

void sendSyntheticMouse(SDL3Mouse *mouse, SDL_Window *window, Uint32 type,
                        float x, float y, Uint8 button = 0, float wheelY = 0.0f)
{
	// The windowID must be valid: SDL3Mouse::scaleMouseCoordinates() looks the
	// window up by id to map window points into the game's internal resolution,
	// and silently skips scaling when the lookup fails.
	const SDL_WindowID windowID = SDL_GetWindowID(window);

	SDL_Event ev;
	SDL_zero(ev);
	ev.type = type;
	switch (type) {
		case SDL_EVENT_MOUSE_MOTION:
			ev.motion.windowID = windowID;
			ev.motion.x = x;
			ev.motion.y = y;
			break;
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
		case SDL_EVENT_MOUSE_BUTTON_UP:
			ev.button.windowID = windowID;
			ev.button.button = button;
			ev.button.down = (type == SDL_EVENT_MOUSE_BUTTON_DOWN);
			ev.button.clicks = 1;
			ev.button.x = x;
			ev.button.y = y;
			break;
		case SDL_EVENT_MOUSE_WHEEL:
			ev.wheel.windowID = windowID;
			ev.wheel.x = 0.0f;
			ev.wheel.y = wheelY;
			ev.wheel.mouse_x = x;
			ev.wheel.mouse_y = y;
			break;
	}
	mouse->addSDLEvent(&ev);
}

void flushPendingUp(SDL3Mouse *mouse, SDL_Window *window)
{
	if (s_touch.pendingUpFrames > 0) {
		sendSyntheticMouse(mouse, window, SDL_EVENT_MOUSE_BUTTON_UP,
		                   s_touch.pendingUpX, s_touch.pendingUpY, s_touch.pendingUpButton);
		s_touch.pendingUpFrames = 0;
	}
}

// Full click at (x, y): motion + down now, up on the next frame.
void sendClick(SDL3Mouse *mouse, SDL_Window *window, float x, float y, Uint8 button)
{
	flushPendingUp(mouse, window);
	sendSyntheticMouse(mouse, window, SDL_EVENT_MOUSE_MOTION, x, y);
	sendSyntheticMouse(mouse, window, SDL_EVENT_MOUSE_BUTTON_DOWN, x, y, button);
	// Released by the frame tick after the next one: the tick of the current frame runs
	// right after this event batch, so 2 means "one full engine frame later".
	s_touch.pendingUpFrames = 2;
	s_touch.pendingUpButton = button;
	s_touch.pendingUpX = x;
	s_touch.pendingUpY = y;
}

void centroid(int winW, int winH, float &cx, float &cy, float &dist)
{
	cx = (s_touch.f1x + s_touch.f2x) * 0.5f * (float)winW;
	cy = (s_touch.f1y + s_touch.f2y) * 0.5f * (float)winH;
	const float dx = (s_touch.f1x - s_touch.f2x) * (float)winW;
	const float dy = (s_touch.f1y - s_touch.f2y) * (float)winH;
	dist = SDL_sqrtf(dx * dx + dy * dy);
}

// Second finger landed: remember where, but send nothing until the gesture shows
// whether it is a camera pan (fingers move together) or a pinch zoom (they spread).
void beginTwoFinger(int winW, int winH)
{
	centroid(winW, winH, s_touch.startCX, s_touch.startCY, s_touch.startDist);
	s_touch.panX = s_touch.startCX;
	s_touch.panY = s_touch.startCY;
	s_touch.pinchDist = s_touch.startDist;
	s_touch.phase = TouchState::TWO_PENDING;
}

void beginDragBox(SDL3Mouse *mouse, SDL_Window *window)
{
	// Anchor the LMB at the original touch point so the box starts where the finger
	// landed. The motion to the current position follows with the next finger event,
	// i.e. in a later frame than the button-down, which the engine's drag detection needs.
	flushPendingUp(mouse, window);
	sendSyntheticMouse(mouse, window, SDL_EVENT_MOUSE_MOTION, s_touch.downX, s_touch.downY);
	sendSyntheticMouse(mouse, window, SDL_EVENT_MOUSE_BUTTON_DOWN,
	                   s_touch.downX, s_touch.downY, SDL_BUTTON_LEFT);
	s_touch.phase = TouchState::DRAGGING;
}

void handleTouchEvent(SDL3Mouse *mouse, SDL_Window *window, const SDL_Event &event)
{
	int winW = 0, winH = 0;
	SDL_GetWindowSize(window, &winW, &winH);
	const float px = event.tfinger.x * (float)winW;
	const float py = event.tfinger.y * (float)winH;

	switch (event.type) {
	case SDL_EVENT_FINGER_DOWN:
		if (s_touch.phase == TouchState::IDLE) {
			// Defer all BUTTON output: a finger landing could become a tap, a
			// drag-box, a long-press, or the first finger of a camera pan. A
			// premature LMB down+up is a real click to the game (e.g. it sets a
			// rally point when a production building is selected).
			s_touch.finger1 = event.tfinger.fingerID;
			s_touch.phase = TouchState::PENDING;
			s_touch.downX = s_touch.lastX = px;
			s_touch.downY = s_touch.lastY = py;
			s_touch.f1x = event.tfinger.x;
			s_touch.f1y = event.tfinger.y;
			s_touch.downTicks = SDL_GetTicks();
			// Move the cursor to the touch point NOW (motion clicks nothing). This lets
			// the GUI process hover before the tap commits: hover-driven widgets (e.g.
			// the Generals Challenge general buttons) ignore a click unless a prior
			// mouse-enter hilited them.
			flushPendingUp(mouse, window);
			sendSyntheticMouse(mouse, window, SDL_EVENT_MOUSE_MOTION, px, py);
		}
		else if (s_touch.phase == TouchState::PENDING || s_touch.phase == TouchState::LONGPRESSED) {
			// Second finger before the first committed to anything: two-finger
			// gesture, no left-click ever happened.
			s_touch.finger2 = event.tfinger.fingerID;
			s_touch.f2x = event.tfinger.x;
			s_touch.f2y = event.tfinger.y;
			beginTwoFinger(winW, winH);
		}
		else if (s_touch.phase == TouchState::DRAGGING) {
			// Second finger during a live drag: finish the drag-box, then two-finger.
			s_touch.finger2 = event.tfinger.fingerID;
			s_touch.f2x = event.tfinger.x;
			s_touch.f2y = event.tfinger.y;
			sendSyntheticMouse(mouse, window, SDL_EVENT_MOUSE_BUTTON_UP,
			                   s_touch.lastX, s_touch.lastY, SDL_BUTTON_LEFT);
			beginTwoFinger(winW, winH);
		}
		// PAN / ZOOM / TWO_PENDING with extra fingers: ignored
		break;

	case SDL_EVENT_FINGER_MOTION: {
		const bool twoFinger = s_touch.phase == TouchState::TWO_PENDING ||
		                       s_touch.phase == TouchState::PAN || s_touch.phase == TouchState::ZOOM;
		if (event.tfinger.fingerID == s_touch.finger1) {
			s_touch.f1x = event.tfinger.x;
			s_touch.f1y = event.tfinger.y;
			s_touch.lastX = px;
			s_touch.lastY = py;
		} else if (twoFinger && event.tfinger.fingerID == s_touch.finger2) {
			s_touch.f2x = event.tfinger.x;
			s_touch.f2y = event.tfinger.y;
		} else {
			break;
		}

		if ((s_touch.phase == TouchState::PENDING || s_touch.phase == TouchState::LONGPRESSED) &&
		    event.tfinger.fingerID == s_touch.finger1) {
			const float moved = SDL_fabsf(px - s_touch.downX) + SDL_fabsf(py - s_touch.downY);
			if (moved >= tapDeadZone(winW, winH)) {
				// Also after a long-press: players often rest a finger before dragging
				// out a selection box; the deselect already happened, now draw the box.
				beginDragBox(mouse, window);
			}
		}
		else if (s_touch.phase == TouchState::DRAGGING && event.tfinger.fingerID == s_touch.finger1) {
			sendSyntheticMouse(mouse, window, SDL_EVENT_MOUSE_MOTION, px, py);
		}
		else if (twoFinger) {
			float cx, cy, dist;
			centroid(winW, winH, cx, cy, dist);

			if (s_touch.phase == TouchState::TWO_PENDING) {
				const float moved = SDL_fabsf(cx - s_touch.startCX) + SDL_fabsf(cy - s_touch.startCY);
				const float spread = s_touch.startDist > 1.0f
					? SDL_fabsf(dist - s_touch.startDist) / s_touch.startDist : 0.0f;
				if (spread >= PINCH_DECIDE_RATIO) {
					s_touch.phase = TouchState::ZOOM;
					s_touch.pinchDist = s_touch.startDist;
					sendSyntheticMouse(mouse, window, SDL_EVENT_MOUSE_MOTION, cx, cy);
				} else if (moved >= tapDeadZone(winW, winH) * 1.5f) {
					// Camera scroll is a right-button drag in this engine.
					sendSyntheticMouse(mouse, window, SDL_EVENT_MOUSE_MOTION, s_touch.startCX, s_touch.startCY);
					sendSyntheticMouse(mouse, window, SDL_EVENT_MOUSE_BUTTON_DOWN,
					                   s_touch.startCX, s_touch.startCY, SDL_BUTTON_RIGHT);
					s_touch.phase = TouchState::PAN;
				}
			}

			if (s_touch.phase == TouchState::PAN) {
				sendSyntheticMouse(mouse, window, SDL_EVENT_MOUSE_MOTION, cx, cy);
				s_touch.panX = cx;
				s_touch.panY = cy;
			}
			else if (s_touch.phase == TouchState::ZOOM && s_touch.pinchDist > 1.0f) {
				// Wheel steps without any button held: the engine ignores the wheel
				// during a right-button camera drag, so zoom never mixes with pan.
				const float ratio = dist / s_touch.pinchDist;
				if (ratio > 1.0f + PINCH_STEP_RATIO) {
					sendSyntheticMouse(mouse, window, SDL_EVENT_MOUSE_WHEEL, cx, cy, 0, 1.0f);
					s_touch.pinchDist = dist;
				} else if (ratio < 1.0f - PINCH_STEP_RATIO) {
					sendSyntheticMouse(mouse, window, SDL_EVENT_MOUSE_WHEEL, cx, cy, 0, -1.0f);
					s_touch.pinchDist = dist;
				}
			}
		}
		break;
	}

	case SDL_EVENT_FINGER_UP:
	case SDL_EVENT_FINGER_CANCELED: {
		const bool twoFinger = s_touch.phase == TouchState::TWO_PENDING ||
		                       s_touch.phase == TouchState::PAN || s_touch.phase == TouchState::ZOOM;
		if (event.tfinger.fingerID != s_touch.finger1 &&
		    !(twoFinger && event.tfinger.fingerID == s_touch.finger2)) {
			break;
		}
		switch (s_touch.phase) {
			case TouchState::PENDING:
				// A CANCELED touch (incoming call, notification shade, palm rejection)
				// must not become a committed tap — that would be a phantom
				// select/command/rally-point click at the cancel point.
				if (event.type == SDL_EVENT_FINGER_CANCELED) {
					break;
				}
				// Clean tap: the full click at the exact press position.
				sendClick(mouse, window, s_touch.downX, s_touch.downY, SDL_BUTTON_LEFT);
				break;
			case TouchState::DRAGGING:
				sendSyntheticMouse(mouse, window, SDL_EVENT_MOUSE_BUTTON_UP, px, py, SDL_BUTTON_LEFT);
				break;
			case TouchState::PAN:
				sendSyntheticMouse(mouse, window, SDL_EVENT_MOUSE_BUTTON_UP,
				                   s_touch.panX, s_touch.panY, SDL_BUTTON_RIGHT);
				break;
			default:
				break;
		}
		s_touch.phase = TouchState::IDLE;
		break;
	}
	}
}

// Called once per engine frame (not just per touch event): a perfectly
// stationary finger produces no SDL events, so the long-press timer must be
// polled from the frame loop or it would never fire. Also releases the button
// of the previous frame's tap.
void updateTouchLongPress(SDL3Mouse *mouse, SDL_Window *window)
{
	if (s_touch.pendingUpFrames > 0 && --s_touch.pendingUpFrames == 0) {
		s_touch.pendingUpFrames = 1;  // let flushPendingUp() send it
		flushPendingUp(mouse, window);
	}

	if (s_touch.phase == TouchState::PENDING &&
	    (SDL_GetTicks() - s_touch.downTicks) >= LONG_PRESS_MS) {
		// No LMB was sent yet (deferred), so this is a pure right-click.
		sendClick(mouse, window, s_touch.downX, s_touch.downY, SDL_BUTTON_RIGHT);
		s_touch.phase = TouchState::LONGPRESSED;
	}
}

} // anonymous namespace
#endif // GX_TOUCH_PLATFORM

namespace {

Bool DecodeNextUtf8Codepoint(const char* text, size_t length, size_t& offset, UnsignedInt& outCodepoint)
{
	outCodepoint = 0;
	if (!text || offset >= length) {
		return false;
	}

	const unsigned char first = static_cast<unsigned char>(text[offset]);
	if (first == 0) {
		return false;
	}

	if (first < 0x80) {
		outCodepoint = first;
		offset += 1;
		return true;
	}

	if ((first & 0xE0) == 0xC0 && offset + 1 < length) {
		const unsigned char second = static_cast<unsigned char>(text[offset + 1]);
		if ((second & 0xC0) == 0x80) {
			outCodepoint = ((first & 0x1F) << 6) | (second & 0x3F);
			offset += 2;
			return true;
		}
	}

	if ((first & 0xF0) == 0xE0 && offset + 2 < length) {
		const unsigned char second = static_cast<unsigned char>(text[offset + 1]);
		const unsigned char third = static_cast<unsigned char>(text[offset + 2]);
		if ((second & 0xC0) == 0x80 && (third & 0xC0) == 0x80) {
			outCodepoint = ((first & 0x0F) << 12) | ((second & 0x3F) << 6) | (third & 0x3F);
			offset += 3;
			return true;
		}
	}

	if ((first & 0xF8) == 0xF0 && offset + 3 < length) {
		const unsigned char second = static_cast<unsigned char>(text[offset + 1]);
		const unsigned char third = static_cast<unsigned char>(text[offset + 2]);
		const unsigned char fourth = static_cast<unsigned char>(text[offset + 3]);
		if ((second & 0xC0) == 0x80 && (third & 0xC0) == 0x80 && (fourth & 0xC0) == 0x80) {
			outCodepoint = ((first & 0x07) << 18) | ((second & 0x3F) << 12) | ((third & 0x3F) << 6) | (fourth & 0x3F);
			offset += 4;
			return true;
		}
	}

	// Invalid UTF-8 sequence: skip one byte and keep processing.
	offset += 1;
	return false;
}

}

/**
 * Constructor: Initialize SDL3 game engine state
 */
SDL3GameEngine::SDL3GameEngine()
	: GameEngine(),
	  m_SDLWindow(nullptr),
	  m_IsInitialized(false),
	  m_IsActive(false),
	  m_IsTextInputActive(false),
	  m_TextInputFocusWindow(nullptr)
{
	fprintf(stderr, "DEBUG: SDL3GameEngine::SDL3GameEngine() created\n");
}

/**
 * Destructor: Cleanup SDL3 resources
 */
SDL3GameEngine::~SDL3GameEngine()
{
	if (m_SDLWindow && m_IsTextInputActive) {
		SDL_StopTextInput(m_SDLWindow);
		m_IsTextInputActive = false;
		m_TextInputFocusWindow = nullptr;
	}

	if (m_IsInitialized) {
		// Window cleanup is done in reset/shutdown
	}
	fprintf(stderr, "DEBUG: SDL3GameEngine::~SDL3GameEngine() destroyed\n");
}

/**
 * From GameEngine: init() - initialize subsystems
 * 
 * GeneralsX @bugfix felipebraz 16/02/2026
 * Simplified to follow fighter19 pattern - SDL3/Vulkan initialized in SDL3Main.cpp
 * before GameEngine is created. This init() only delegates to parent GameEngine::init().
 * ApplicationHWnd and TheSDL3Window are already set by main() before this is called.
 */
void SDL3GameEngine::init(void)
{
	fprintf(stderr, "INFO: SDL3GameEngine::init() starting\n");

	if (TheGlobalData && TheGlobalData->m_headless) {
		// GeneralsX @bugfix Copilot 17/05/2026 Allow headless replay path to initialize engine subsystems without an SDL window.
		fprintf(stderr, "INFO: SDL3GameEngine::init() headless mode - skipping SDL window binding\n");
		m_SDLWindow = nullptr;
		m_IsInitialized = true;
		m_IsActive = true;
		GameEngine::init();
		return;
	}

	// Verify window was created by SDL3Main.cpp
	extern SDL_Window* TheSDL3Window;
	extern HWND ApplicationHWnd;
	
	if (!TheSDL3Window || !ApplicationHWnd) {
		fprintf(stderr, "FATAL: SDL3 window not initialized before GameEngine::init()\n");
		fprintf(stderr, "FATAL: TheSDL3Window=%p, ApplicationHWnd=%p\n", TheSDL3Window, ApplicationHWnd);
		return;
	}

	// Store window reference locally
	m_SDLWindow = TheSDL3Window;
	m_IsInitialized = true;
	m_IsActive = true;

#if GX_TOUCH_PLATFORM
	// Lifecycle events can fire outside the poll cycle on iOS; catch them
	// immediately so rendering halts before the process is suspended.
	SDL_AddEventWatch(iosLifecycleWatcher, nullptr);
#endif

	fprintf(stderr, "INFO: SDL3GameEngine using pre-initialized window\n");

	// Call parent init to initialize game subsystems
	GameEngine::init();
}

/**
 * From GameEngine: reset() - reset system to starting state
 */
void SDL3GameEngine::reset(void)
{
	fprintf(stderr, "DEBUG: SDL3GameEngine::reset()\n");
	if (m_SDLWindow && m_IsTextInputActive) {
		SDL_StopTextInput(m_SDLWindow);
		m_IsTextInputActive = false;
		m_TextInputFocusWindow = nullptr;
	}
	GameEngine::reset();
}

/**
 * From GameEngine: update() - per-frame update
 */
void SDL3GameEngine::update(void)
{
	pollSDL3Events();
#if GX_TOUCH_PLATFORM
	// Pause sim + render while backgrounded OR inactive (see iosLifecycleWatcher).
	// Acquiring a Metal drawable in these windows fights iOS for the layer and,
	// across repeated suspend/switcher cycles, crashes MoltenVK. Keep polling so
	// we still catch the resume events; just don't touch the GPU.
	if (iosShouldPauseRendering()) {
		SDL_Delay(50);
		return;
	}
#endif
	GameEngine::update();
}

/**
 * From GameEngine: execute() - main game loop
 */
void SDL3GameEngine::execute(void)
{
	fprintf(stderr, "INFO: SDL3GameEngine::execute() - entering main loop\n");
	GameEngine::execute();
	fprintf(stderr, "INFO: SDL3GameEngine::execute() - exited main loop\n");
}

/**
 * From GameEngine: serviceWindowsOS() - native OS service
 * On Linux, process SDL3 events
 */
void SDL3GameEngine::serviceWindowsOS(void)
{
	pollSDL3Events();
}

/**
 * Check if game has OS focus
 */
Bool SDL3GameEngine::isActive(void)
{
	return m_IsActive;
}

/**
 * Set OS focus status
 */
void SDL3GameEngine::setIsActive(Bool isActive)
{
	m_IsActive = isActive;
}

/**
 * Poll and process SDL3 events
 * Handles keyboard, mouse, window, and quit events
 */
void SDL3GameEngine::pollSDL3Events(void)
{
	if (!m_SDLWindow) {
		return;
	}

	updateTextInputState();

	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		switch (event.type) {
			case SDL_EVENT_QUIT:
				m_quitting = true;
				break;

			case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
				m_quitting = true;
				break;

			case SDL_EVENT_WINDOW_FOCUS_GAINED:
				m_IsActive = true;
				if (TheMouse) {
					TheMouse->regainFocus();
					TheMouse->refreshCursorCapture();
				}
				break;

			case SDL_EVENT_WINDOW_FOCUS_LOST:
				m_IsActive = false;
				if (m_IsTextInputActive) {
					SDL_StopTextInput(m_SDLWindow);
					m_IsTextInputActive = false;
					m_TextInputFocusWindow = nullptr;
				}
				if (TheMouse) {
					TheMouse->loseFocus();
				}
				break;

#if GX_TOUCH_PLATFORM
			// App suspension/resume: mirror the desktop focus handling so audio
			// and mouse state pause cleanly (the render gate lives in update()).
			case SDL_EVENT_DID_ENTER_BACKGROUND:
				m_IsActive = false;
				if (TheMouse) {
					TheMouse->loseFocus();
				}
				break;

			case SDL_EVENT_DID_ENTER_FOREGROUND:
				m_IsActive = true;
				if (TheMouse) {
					TheMouse->regainFocus();
					TheMouse->refreshCursorCapture();
				}
				break;
#endif

			case SDL_EVENT_WINDOW_MOUSE_ENTER:
				if (TheMouse) {
					TheMouse->onCursorMovedInside();
				}
				break;

			case SDL_EVENT_WINDOW_MOUSE_LEAVE:
				if (TheMouse) {
					TheMouse->onCursorMovedOutside();
				}
				break;

			case SDL_EVENT_KEY_DOWN:
			case SDL_EVENT_KEY_UP:
#if defined(__ANDROID__)
				// ZH Mobile: the Android Back button/gesture acts as Escape
				// (opens the in-game menu / goes back in shell menus).
				// SDL_HINT_ANDROID_TRAP_BACK_BUTTON is set in SDL3Main.cpp.
				if (event.key.scancode == SDL_SCANCODE_AC_BACK || event.key.key == SDLK_AC_BACK) {
					event.key.scancode = SDL_SCANCODE_ESCAPE;
					event.key.key = SDLK_ESCAPE;
				}
#endif
				// Fighter19 pattern: direct addSDLEvent() call
				// GeneralsX @refactor felipebraz 16/02/2026 Simplified event routing
				if (TheKeyboard) {
					SDL3Keyboard* keyboard = dynamic_cast<SDL3Keyboard*>(TheKeyboard);
					if (keyboard) {
						keyboard->addSDLEvent(&event);
					}
				}
				break;

			case SDL_EVENT_TEXT_INPUT:
				forwardTextInputEvent(event.text.text);
				break;

			case SDL_EVENT_MOUSE_MOTION:
			case SDL_EVENT_MOUSE_BUTTON_DOWN:
			case SDL_EVENT_MOUSE_BUTTON_UP:
			case SDL_EVENT_MOUSE_WHEEL:
#if GX_TOUCH_PLATFORM
				// Belt-and-braces: drop SDL's own touch-synthesized mouse events.
				// The gesture translator owns all touch->mouse conversion; double
				// delivery would produce phantom second clicks.
				if (event.motion.which == SDL_TOUCH_MOUSEID) {
					break;
				}
#endif
				// Fighter19 pattern: direct addSDLEvent() call with raw SDL_Event
				// GeneralsX @refactor felipebraz 16/02/2026 Simplified event routing
				if (TheMouse) {
					SDL3Mouse* mouse = dynamic_cast<SDL3Mouse*>(TheMouse);
					if (mouse) {
						mouse->addSDLEvent(&event);
					}
				}
				break;

#if GX_TOUCH_PLATFORM
			case SDL_EVENT_FINGER_DOWN:
			case SDL_EVENT_FINGER_MOTION:
			case SDL_EVENT_FINGER_UP:
			case SDL_EVENT_FINGER_CANCELED:
				if (TheMouse && m_SDLWindow) {
					SDL3Mouse* mouse = dynamic_cast<SDL3Mouse*>(TheMouse);
					if (mouse) {
						handleTouchEvent(mouse, m_SDLWindow, event);
					}
				}
				break;
#endif

			case SDL_EVENT_WINDOW_RESIZED:
				handleWindowEvent(event.window);
				break;

			default:
				// Ignore other events for now
				break;
		}

		updateTextInputState();
	}

#if GX_TOUCH_PLATFORM
	// Poll the long-press timer every frame; a stationary finger emits no events.
	if (TheMouse && m_SDLWindow) {
		SDL3Mouse* touchMouse = dynamic_cast<SDL3Mouse*>(TheMouse);
		if (touchMouse) {
			updateTouchLongPress(touchMouse, m_SDLWindow);
		}
	}
#endif
}

// GeneralsX @bugfix felipebraz 01/04/2026 Enable SDL text input only while an entry gadget owns focus.
void SDL3GameEngine::updateTextInputState(void)
{
	if (!m_SDLWindow || !TheWindowManager) {
		return;
	}

	GameWindow* focusedWindow = TheWindowManager->winGetFocus();
	const Bool wantsTextInput =
		focusedWindow != nullptr && BitIsSet(focusedWindow->winGetStyle(), GWS_ENTRY_FIELD);

	if (wantsTextInput) {
		if (!m_IsTextInputActive) {
			if (SDL_StartTextInput(m_SDLWindow)) {
				m_IsTextInputActive = true;
			}
		}
		m_TextInputFocusWindow = focusedWindow;
	} else {
		if (m_IsTextInputActive) {
			SDL_StopTextInput(m_SDLWindow);
			m_IsTextInputActive = false;
		}
		m_TextInputFocusWindow = nullptr;
	}
}

// GeneralsX @bugfix felipebraz 01/04/2026 Forward SDL UTF-8 text input through existing GWM_IME_CHAR path.
void SDL3GameEngine::forwardTextInputEvent(const char* utf8Text)
{
	if (!utf8Text || !TheWindowManager) {
		return;
	}

	// GeneralsX @bugfix felipebraz 01/04/2026 Use tracked text-input focus window to keep SDL text delivery stable.
	GameWindow* targetWindow = m_TextInputFocusWindow;
	if (!targetWindow || !BitIsSet(targetWindow->winGetStyle(), GWS_ENTRY_FIELD)) {
		return;
	}

	const size_t textLength = strlen(utf8Text);
	size_t offset = 0;
	while (offset < textLength) {
		UnsignedInt codepoint = 0;
		if (!DecodeNextUtf8Codepoint(utf8Text, textLength, offset, codepoint)) {
			continue;
		}

		// GeneralsX @bugfix felipebraz 01/04/2026 Clamp IME char forwarding to BMP and reject UTF-16 surrogate range.
		if (codepoint == 0 || codepoint > 0x10FFFFU) {
			continue;
		}

		if (codepoint >= 0xD800U && codepoint <= 0xDFFFU) {
			continue;
		}

		if (codepoint > 0xFFFFU) {
			continue;
		}

		const WideChar wideCharacter = static_cast<WideChar>(codepoint);
		TheWindowManager->winSendInputMsg(targetWindow, GWM_IME_CHAR, static_cast<WindowMsgData>(wideCharacter), 0);
	}
}

/**
 * Handle keyboard event -dispatch to Keyboard manager
 * TheSuperHackers @build 10/02/2026 BenderAI - Phase 1.5 event wiring
 */
void SDL3GameEngine::handleKeyboardEvent(const SDL_KeyboardEvent& event)
{
	// Dispatch to SDL3Keyboard if available
	if (TheKeyboard) {
		SDL3Keyboard* sdlKeyboard = dynamic_cast<SDL3Keyboard*>(TheKeyboard);
		if (sdlKeyboard) {
			sdlKeyboard->addSDL3KeyEvent(event);
		}
	}
}

/**
 * Handle mouse motion event - dispatch to Mouse manager
 * TheSuperHackers @build 10/02/2026 BenderAI - Phase 1.5 event wiring
 */
void SDL3GameEngine::handleMouseMotionEvent(const SDL_MouseMotionEvent& event)
{
	// Dispatch to SDL3Mouse if available
	if (TheMouse) {
		SDL3Mouse* sdlMouse = dynamic_cast<SDL3Mouse*>(TheMouse);
		if (sdlMouse) {
			sdlMouse->addSDL3MouseMotionEvent(event);
		}
	}
}

/**
 * Handle mouse button event - dispatch to Mouse manager
 * TheSuperHackers @build 10/02/2026 BenderAI - Phase 1.5 event wiring
 */
void SDL3GameEngine::handleMouseButtonEvent(const SDL_MouseButtonEvent& event)
{
	// Dispatch to SDL3Mouse if available
	if (TheMouse) {
		SDL3Mouse* sdlMouse = dynamic_cast<SDL3Mouse*>(TheMouse);
		if (sdlMouse) {
			sdlMouse->addSDL3MouseButtonEvent(event);
		}
	}
}

/**
 * Handle mouse wheel event - dispatch to Mouse manager
 * TheSuperHackers @build 10/02/2026 BenderAI - Phase 1.5 event wiring
 */
void SDL3GameEngine::handleMouseWheelEvent(const SDL_MouseWheelEvent& event)
{
	// Dispatch to SDL3Mouse if available
	if (TheMouse) {
		SDL3Mouse* sdlMouse = dynamic_cast<SDL3Mouse*>(TheMouse);
		if (sdlMouse) {
			sdlMouse->addSDL3MouseWheelEvent(event);
		}
	}
}

/**
 * Handle window event (resize, etc.)
 */
void SDL3GameEngine::handleWindowEvent(const SDL_WindowEvent& event)
{
	// TODO: Phase 2 - Handle window resize, notify graphics subsystem
	// fprintf(stderr, "DEBUG: Window event (type=%d)\n", event.type);
}

/**
 * Factory Methods for GameEngine subsystems
 * TheSuperHackers @build felipebraz 13/02/2026
 * Implementations in .cpp to provide complete type definitions and avoid circular includes
 */

LocalFileSystem *SDL3GameEngine::createLocalFileSystem(void)
{
	fprintf(stderr, "INFO: SDL3GameEngine::createLocalFileSystem() -> StdLocalFileSystem\n");
	return NEW StdLocalFileSystem;
}

ArchiveFileSystem *SDL3GameEngine::createArchiveFileSystem(void)
{
	fprintf(stderr, "INFO: SDL3GameEngine::createArchiveFileSystem() -> StdBIGFileSystem\n");
	return NEW StdBIGFileSystem;
}

GameLogic *SDL3GameEngine::createGameLogic(void)
{
	fprintf(stderr, "INFO: SDL3GameEngine::createGameLogic() -> W3DGameLogic\n");
	return NEW W3DGameLogic;
}

GameClient *SDL3GameEngine::createGameClient(void)
{
	fprintf(stderr, "INFO: SDL3GameEngine::createGameClient() -> W3DGameClient\n");
	return NEW W3DGameClient;
}

ModuleFactory *SDL3GameEngine::createModuleFactory(void)
{
	fprintf(stderr, "INFO: SDL3GameEngine::createModuleFactory() -> W3DModuleFactory\n");
	return NEW W3DModuleFactory;
}

ThingFactory *SDL3GameEngine::createThingFactory(void)
{
	fprintf(stderr, "INFO: SDL3GameEngine::createThingFactory() -> W3DThingFactory\n");
	return NEW W3DThingFactory;
}

FunctionLexicon *SDL3GameEngine::createFunctionLexicon(void)
{
	fprintf(stderr, "INFO: SDL3GameEngine::createFunctionLexicon() -> W3DFunctionLexicon\n");
	return NEW W3DFunctionLexicon;
}

// GeneralsX @bugfix Copilot 15/04/2026 Match upstream GameEngine pure-virtual signature after sync.
Radar *SDL3GameEngine::createRadar(Bool dummy)
{
	// GeneralsX @bugfix fbraz 04/05/2026 Respect headless mode and create dummy radar.
	// Upstream reference: Win32GameEngine headless factory behavior, TheSuperHackers/GeneralsGameCode
	// https://github.com/TheSuperHackers/GeneralsGameCode
	if (dummy) {
		fprintf(stderr, "INFO: SDL3GameEngine::createRadar() -> RadarDummy (headless)\n");
		return NEW RadarDummy;
	}
	fprintf(stderr, "INFO: SDL3GameEngine::createRadar() -> W3DRadar\n");
	return NEW W3DRadar;
}

// GeneralsX @bugfix Copilot 24/03/2026 Match upstream GameEngine pure-virtual signature after sync.
ParticleSystemManager* SDL3GameEngine::createParticleSystemManager(Bool dummy)
{
	// GeneralsX @bugfix fbraz 04/05/2026 Respect headless mode and create dummy particle manager.
	if (dummy) {
		fprintf(stderr, "INFO: SDL3GameEngine::createParticleSystemManager() -> ParticleSystemManagerDummy (headless)\n");
		return NEW ParticleSystemManagerDummy;
	}
	fprintf(stderr, "INFO: SDL3GameEngine::createParticleSystemManager() -> W3DParticleSystemManager\n");
	return NEW W3DParticleSystemManager;
}

WebBrowser *SDL3GameEngine::createWebBrowser(void)
{
	// WebBrowser uses Windows COM (CComObject<W3DWebBrowser>)
	// Not available on Linux - return nullptr
	fprintf(stderr, "WARNING: WebBrowser not available on Linux platform\n");
	return nullptr;
}

/**
 * Factory method: AudioManager
 * Select audio backend based on compile flags
 * GeneralsX @bugfix Copilot 15/04/2026 Match upstream GameEngine pure-virtual signature after sync.
 */
AudioManager *SDL3GameEngine::createAudioManager(Bool dummy)
{
	(void)dummy;
	fprintf(stderr, "INFO: SDL3GameEngine::createAudioManager()\n");

#ifdef SAGE_USE_OPENAL
	fprintf(stderr, "INFO: Creating OpenAL audio backend\n");
	return new OpenALAudioManager();
#else
	fprintf(stderr, "INFO: Audio backend not available (SAGE_USE_OPENAL not defined)\n");
	fprintf(stderr, "WARNING: Falls back to parent implementation or silent mode\n");
	return GameEngine::createAudioManager();  // Call parent (may return stub)
#endif
}

#endif // !_WIN32


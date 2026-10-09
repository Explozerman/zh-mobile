package io.github.explozerman.zhmobile;

import android.content.Context;
import android.content.SharedPreferences;

import java.util.HashMap;
import java.util.Locale;
import java.util.Map;

/**
 * Launcher texts in Russian and English. The language is switchable inside the app
 * (independent of the phone's system language) and is also passed to the game, which
 * then loads the player's Russian language pack if they imported one.
 */
public final class L10n {
    public static final String RU = "ru";
    public static final String EN = "en";

    private static final Map<String, String> ru = new HashMap<>();
    private static final Map<String, String> en = new HashMap<>();

    private static void put(String key, String ruText, String enText) {
        ru.put(key, ruText);
        en.put(key, enText);
    }

    static {
        put("subtitle",
            "Неофициальный порт Zero Hour для Android",
            "Unofficial Zero Hour port for Android");
        put("play", "ИГРАТЬ", "PLAY");
        put("import", "Импортировать файлы игры", "Import game files");
        put("language", "Язык", "Language");
        put("resolution", "Разрешение рендеринга", "Render resolution");
        put("res_hint",
            "Меньше — быстрее и холоднее телефон, больше — чётче картинка.",
            "Lower is faster and cooler, higher is sharper.");
        put("intro", "Заставка при запуске", "Intro videos");
        put("intro_skip", "Пропускать", "Skip");
        put("intro_show", "Показывать", "Show");
        put("save_log", "Сохранить лог в «Загрузки»", "Save log to Downloads");
        put("help", "Как установить игру", "How to install the game");
        put("about", "О программе и лицензии", "About & licenses");
        put("status_ok", "✔ Файлы игры найдены", "✔ Game files found");
        put("status_missing",
            "✖ Файлы игры не найдены. Нажмите «Импортировать файлы игры».",
            "✖ Game files not found. Tap \"Import game files\".");
        put("status_zh_missing", "✖ Не хватает файлов Zero Hour", "✖ Zero Hour files are missing");
        put("status_base_missing",
            "✖ Не хватает файлов основной игры Generals (нужны для Zero Hour)",
            "✖ Base Generals files are missing (Zero Hour needs them)");
        put("status_ru_ok", "✔ Русский языковой пакет установлен", "✔ Russian language pack installed");
        put("status_ru_missing",
            "Русского пакета нет — игра будет на английском (меню лаунчера на русском).",
            "No Russian pack — the game will be in English.");
        put("pick_title", "Выберите папку с игрой", "Choose the game folder");
        put("pick_hint",
            "Выберите папку, в которой лежат «Command and Conquer Generals» и «...Zero Hour» "
            + "(или папку Steam-версии Zero Hour). Копирование займёт несколько минут.",
            "Choose the folder that contains \"Command and Conquer Generals\" and \"...Zero Hour\" "
            + "(or the Steam Zero Hour folder). Copying takes a few minutes.");
        put("ok", "OK", "OK");
        put("cancel", "Отмена", "Cancel");
        put("continue", "Продолжить", "Continue");
        put("importing", "Копирование файлов игры…", "Copying game files…");
        put("import_done", "Готово! Файлы игры импортированы.", "Done! Game files imported.");
        put("import_failed", "Ошибка импорта: ", "Import failed: ");
        put("import_no_zh",
            "В выбранной папке не найдена Zero Hour (файл INIZH.big). Выберите папку с игрой.",
            "Zero Hour was not found in that folder (INIZH.big). Choose the game folder.");
        put("import_no_base",
            "Zero Hour найдена, но нет основной игры Generals (файл INI.big). "
            + "Выберите общую папку, где лежат обе игры.",
            "Zero Hour found, but base Generals is missing (INI.big). "
            + "Choose the parent folder that contains both games.");
        put("import_space", "Недостаточно места. Нужно: ", "Not enough storage. Needed: ");
        put("import_busy", "Импорт уже идёт…", "Import is already running…");
        put("log_saved", "Лог сохранён в «Загрузки»: ", "Log saved to Downloads: ");
        put("log_missing", "Лога пока нет — сначала запустите игру.", "No log yet — start the game first.");
        put("no_vulkan",
            "Этот телефон не поддерживает Vulkan 1.1 — игра на нём не запустится.",
            "This phone does not support Vulkan 1.1 — the game cannot run on it.");
        put("help_text",
            "1. Скопируйте с компьютера на телефон обе папки игры: «Command and Conquer Generals» "
            + "и «Command and Conquer Generals Zero Hour» (например, в одну папку внутри «Загрузок»).\n\n"
            + "2. Нажмите «Импортировать файлы игры» и выберите папку, в которой лежат обе игры.\n\n"
            + "3. Если у вас русская версия (файл 00RussianZH.big), русский пакет установится сам.\n\n"
            + "4. После импорта скопированные папки в «Загрузках» можно удалить.\n\n"
            + "Управление: тап — выбрать/приказ, протянуть пальцем — рамка выделения, "
            + "долгое нажатие — отменить выбор, два пальца — двигать камеру, щипок — зум, "
            + "кнопка «Назад» — меню.",
            "1. Copy both game folders from your PC to the phone: \"Command and Conquer Generals\" "
            + "and \"Command and Conquer Generals Zero Hour\" (e.g. into one folder in Downloads).\n\n"
            + "2. Tap \"Import game files\" and choose the folder that contains both games.\n\n"
            + "3. A Russian language pack (00RussianZH.big) is detected and installed automatically.\n\n"
            + "4. After importing you can delete the copied folders from Downloads.\n\n"
            + "Controls: tap — select/command, drag — selection box, long-press — deselect, "
            + "two fingers — move camera, pinch — zoom, Back button — menu.");
        put("about_text",
            "ZH Mobile — неофициальный некоммерческий порт. Не связан с Electronic Arts и не одобрен ею.\n\n"
            + "Код движка: исходники Command & Conquer Generals/Zero Hour, открытые EA по лицензии GPL v3, "
            + "с доработками сообществ TheSuperHackers, Fighter19, GeneralsX (fbraz3) и iOS-порта (ammaarreshi). "
            + "Этот порт тоже распространяется по GPL v3; исходный код: github.com/Explozerman/zh-mobile\n\n"
            + "Игровые данные (графика, звук, тексты) в приложение не входят: используйте свою лицензионную копию игры.\n\n"
            + "Также используются: SDL3 (zlib), DXVK (zlib), OpenAL Soft (LGPL), FFmpeg (LGPL), FreeType (FTL), "
            + "шрифты Liberation (SIL OFL).",
            "ZH Mobile is an unofficial, non-commercial port. It is not affiliated with or endorsed by Electronic Arts.\n\n"
            + "Engine code: the Command & Conquer Generals/Zero Hour source released by EA under GPL v3, "
            + "with work by TheSuperHackers, Fighter19, GeneralsX (fbraz3) and the iOS port (ammaarreshi). "
            + "This port is GPL v3 as well; source: github.com/Explozerman/zh-mobile\n\n"
            + "No game data (art, audio, text) is included: use your own licensed copy of the game.\n\n"
            + "Also uses: SDL3 (zlib), DXVK (zlib), OpenAL Soft (LGPL), FFmpeg (LGPL), FreeType (FTL), "
            + "Liberation fonts (SIL OFL).");
    }

    private L10n() {}

    static SharedPreferences prefs(Context ctx) {
        return ctx.getSharedPreferences("zhmobile", Context.MODE_PRIVATE);
    }

    /** Current language: saved choice, else the phone's language (Russian or English). */
    public static String lang(Context ctx) {
        String saved = prefs(ctx).getString("lang", null);
        if (saved != null) {
            return saved;
        }
        return Locale.getDefault().getLanguage().equals("ru") ? RU : EN;
    }

    public static void setLang(Context ctx, String lang) {
        prefs(ctx).edit().putString("lang", lang).apply();
    }

    public static String t(Context ctx, String key) {
        Map<String, String> table = RU.equals(lang(ctx)) ? ru : en;
        String s = table.get(key);
        return s != null ? s : key;
    }
}

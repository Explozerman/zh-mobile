set(GS_OPENSSL FALSE)
set(GAMESPY_SERVER_NAME "server.cnc-online.net")

FetchContent_Declare(
    gamespy
    GIT_REPOSITORY https://github.com/TheAssemblyArmada/GamespySDK.git
    GIT_TAG        07e3d15c500415abc281efb74322ab6d9c857eb8
)

FetchContent_MakeAvailable(gamespy)

# ZH Mobile: bionic (Android libc) has no pthread_cancel. GameSpy only uses it to stop
# its own helper threads, which never matters on Android (no GameSpy servers either).
if(ANDROID AND TARGET gscommon)
    target_compile_definitions(gscommon PRIVATE "pthread_cancel(t)=((void)(t), 0)")
endif()

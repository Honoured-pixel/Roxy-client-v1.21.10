#ifdef _WIN32
    #include <Windows.h>
#elif __linux__
    #include <X11/Xlib.h>
    #include <X11/Xutil.h>
#endif

#include <thread>
#include <iostream>
#include <sstream>
#include <string>
#include "Console.hpp"
#include "JarLoader/JarLoader.hpp"
#include "JNI/JNI.hpp"
#include "LightCoreJar/LightCore.jar.hpp"
#include "Java/Java.hpp"
#include "Transformer/Transformer.hpp"
#include "LightCoreMappings.hpp"

#ifdef __linux__
static Display* display = nullptr;
#endif

static bool is_uninject_key_pressed()
{
#ifdef _WIN32
    return GetAsyncKeyState(VK_END);
#elif __linux__
    static KeyCode keycode = XKeysymToKeycode(display, XK_End);

    char key_states[32] = { '\0'};
    XQueryKeymap(display, key_states);

    return ( key_states[keycode << 3] & ( 1 << (keycode & 7) ) );
#endif
}

static void runLightCore()
{
    JNI jni{};
    if (!jni) return;

    {
        LocalFrame frame(jni);

        Java::ClassLoader minecraftClassLoader = Java::ClassLoader(jni.get_class_loader(std::string(LightCoreMappings::net_minecraft_client_MinecraftClient)), jni);
        Console::log_success((std::ostringstream() << "minecraft ClassLoader: " << (void*)minecraftClassLoader).str());


        Java::URLClassLoader lightCoreClassLoader = Java::URLClassLoader::new_object(jni, "file:///C:/Windows/win.ini", minecraftClassLoader);
        Console::log_success((std::ostringstream() << "Light-Core ClassLoader: " << (void*)lightCoreClassLoader).str());

        JarLoader jarLoader{ jni, lightCoreClassLoader };
        if (!jarLoader) return;

        {
            LocalFrame frame(jni);
            if (!jarLoader.load_jar(LightCore_jar.data(), LightCore_jar.size()))
                return;
            const Java::Class& MainClass = lightCoreClassLoader.findLoadedClass("light/core/Main");
            if (!MainClass)
            {
                jni.describe_error();
                return;
            }
            const Java::MethodID& main_ID = MainClass.getStaticMethodID("init", "()V");
            main_ID.invoke<void>(MainClass);
        }
        
        Console::log_success("Loaded Jar");

        Transformer transformer{ jni, minecraftClassLoader };
        if (!transformer) return;
        transformer.retransform();

        Console::log_success("Retransformed");

        while (!is_uninject_key_pressed())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        {
            LocalFrame frame(jni);
            const Java::Class& MainClass = lightCoreClassLoader.findLoadedClass("light/core/Main");
            const Java::MethodID& main_ID = MainClass.getStaticMethodID("shutdown", "()V");
            main_ID.invoke<void>(MainClass);
        }
        Console::log_success("Received end key, waiting 1 second");
    }
    Console::log_success("Garbage collecting");

    jni.get_jvmti_env()->ForceGarbageCollection();
    jclass mainClass = jni.find_class_any_cl("light/core/Main");
    if (!mainClass)
        Console::log_success("Unloaded classes");
    else
        Console::log_error("Failed to unload classes");
}

static void lightCoreMain(void* modaddr)
{
#ifdef __linux__
    display = XOpenDisplay(NULL);
#endif

    Console console{ modaddr };
    if (!console) return;
    runLightCore();

#ifdef __linux__
    XCloseDisplay(display);
#endif
}

#ifdef _WIN32

BOOL WINAPI DllMain(
    HINSTANCE hinstDLL,
    DWORD fdwReason,
    LPVOID lpvReserved)
{
    switch (fdwReason)
    {
    case DLL_PROCESS_ATTACH:
        std::thread(lightCoreMain, hinstDLL).detach();
        break;

    case DLL_THREAD_ATTACH:
        break;

    case DLL_THREAD_DETACH:
        break;

    case DLL_PROCESS_DETACH:
        if (lpvReserved != nullptr)
        {
            break;
        }
        break;
    }
    return TRUE;
}

#elif __linux__

void __attribute__((constructor)) onload_linux()
{
    std::thread(lightCoreMain, nullptr).detach();
    return;
}
void __attribute__((destructor)) onunload_linux()
{
    return;
}

#endif

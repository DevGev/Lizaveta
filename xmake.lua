add_rules("mode.debug", "mode.release")

set_languages("c11")

add_rules("plugin.compile_commands.autoupdate")

add_moduledirs("xmake")

option("icons")
    set_default(false)
    set_showmenu(true)
    set_description("Enable icon support (requires NanoSVG)")
option_end()

if has_config("icons") then
    add_requires("nanosvg")
end

local function default_prefix()
    local home = os.getenv("HOME")
    return home and path.join(home, ".local") or "/usr/local"
end

target("lizaveta")
    set_kind("binary")

    add_includedirs("source")
    add_defines("_GNU_SOURCE")

    add_files("source/*.c")
    add_files("source/app/*.c")
    add_files("source/apps/*.c")
    add_files("source/fs/*.c")
    add_files("source/ui/*.c")
    add_files("source/dbus/*.c")
    add_files("source/icons/*.c")

    if has_config("icons") then
        add_packages("nanosvg")
        add_defines("ICON_SUPPORT")
    end

    set_warnings("all", "extra")

    set_installdir(default_prefix())

    on_load(function (target)
        import("lib.detect.find_package")
        for _, pkg in ipairs({"x11", "xft", "xrender", "xrandr", "fontconfig", "libudev", "dbus-1"}) do
            local package = find_package(pkg)
            if package then
                target:add(package)
            else
                raise("missing required package: " .. pkg)
            end
        end
    end)

    before_install(function (target)
        import("install")
        install.before(target)
    end)

    after_install(function (target)
        import("install")
        install(target)
    end)

    after_uninstall(function (target)
        import("install")
        install.uninstall(target)
    end)

task("setup-portal")
    set_menu {
        usage = "xmake setup-portal",
        description = "Prefer lizaveta for the file chooser portal (run as your normal user)."
    }
    on_run(function ()
        import("desktop")
        if desktop.running_as_root() then
            raise("run this as your normal user, not as root")
        end
        desktop.wire_portal()
    end)

task("setup-defaults")
    set_menu {
        usage = "xmake setup-defaults",
        description = "Make lizaveta the default file manager and route Firefox through its picker."
    }
    on_run(function ()
        import("desktop")
        desktop.setup_defaults()
    end)

add_rules("mode.debug", "mode.release")

set_languages("c11")

target("lizaveta")
    set_kind("binary")

    add_includedirs("source")
    add_defines("_GNU_SOURCE")

    add_files("source/*.c")
    add_files("source/app/*.c")
    add_files("source/fs/*.c")
    add_files("source/ui/*.c")
    add_files("source/dbus/*.c")

    set_warnings("all", "extra")

    on_load(function (target)
        import("lib.detect.find_package")
        for _, pkg in ipairs({"x11", "xft", "xrender", "fontconfig", "libudev", "dbus-1"}) do
            local package = find_package(pkg)
            if package then
                target:add(package)
            else
                raise("missing required package: " .. pkg)
            end
        end
    end)

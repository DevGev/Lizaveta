-- install.lua - what `xmake install` and `xmake uninstall` do beyond
-- copying the binary. Driven from xmake.lua's after_install and
-- after_uninstall hooks.

import("desktop", {rootdir = os.scriptdir()})

local function installed_binary(target)
    return path.join(target:installdir(), "bin", path.filename(target:targetfile()))
end

-- Clears the way for the binary to be replaced. Without this, every install
-- after the portal has been used once fails with "Text file busy".
function before(target)
    local bin = installed_binary(target)
    if not os.isfile(bin) then
        return
    end

    desktop.stop_portal_service(bin)

    local holders = desktop.processes_using(bin)
    if holders then
        raise("%s is still running (pid %s). Close it and install again.", bin, holders)
    end
end

-- Places the desktop entries and D-Bus services, then wires the file
-- chooser portal to lizaveta. A privileged install stops after the files:
-- the wiring belongs to a person, not to root, and doing it here would
-- leave root-owned files in whoever's home directory happened to be set.
function main(target)
    local prefix = target:installdir()
    local share = path.join(prefix, "share")
    local bin = installed_binary(target)

    desktop.install_files(bin, share)

    if desktop.running_as_root() then
        cprint("")
        cprint("${yellow}note:${clear} installed as root, so nothing under $HOME was touched.")
        cprint("      Run ${bright}xmake setup-portal${clear} as your normal user to prefer")
        cprint("      lizaveta for the file chooser portal.")
        return
    end
    desktop.wire_portal()

    cprint("")
    cprint("Installed to ${bright}%s${clear}.", prefix)
    cprint("Run ${bright}xmake setup-defaults${clear} to also make lizaveta your default")
    cprint("file manager and route Firefox through its picker.")

    local bindir = path.join(prefix, "bin")
    if not (os.getenv("PATH") or ""):find(bindir, 1, true) then
        cprint("${yellow}note:${clear} %s is not on your PATH.", bindir)
    end
end

-- Removes only what install_files wrote. Personal configuration is not
-- unwound: portals.conf may have been edited by hand since, and the backup
-- taken at install time is the honest way back.
function uninstall(target)
    desktop.remove_files(path.join(target:installdir(), "share"))

    cprint("")
    cprint("Personal configuration is left alone. To undo it, restore")
    cprint("%s from its .bak.",
           path.join(desktop.config_home(), "xdg-desktop-portal/portals.conf"))
end

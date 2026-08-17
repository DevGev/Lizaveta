-- desktop.lua - registers lizaveta with the desktop: the application menu
-- entry, the D-Bus services behind the file chooser portal, and the
-- personal preferences that route other apps to them.
--
-- Imported from xmake.lua; the helpers live here rather than there because
-- only a script sandbox has try/catch, which the description scope where
-- xmake.lua's own locals are compiled does not.

-- The files lizaveta owns, relative to <prefix>/share. Listed once so
-- install and uninstall cannot drift apart.
function files()
    return {
        "applications/lizaveta.desktop",
        "applications/lizaveta-filechooser.desktop",
        "dbus-1/services/org.freedesktop.impl.portal.desktop.lizaveta.service",
        "dbus-1/services/org.freedesktop.FileManager1.service",
        "xdg-desktop-portal/portals/lizaveta.portal",
    }
end

function config_home()
    return os.getenv("XDG_CONFIG_HOME") or path.join(os.getenv("HOME") or "/", ".config")
end

function data_home()
    return os.getenv("XDG_DATA_HOME")
           or path.join(os.getenv("HOME") or "/", ".local/share")
end

-- Best effort: these are refresh and notification steps, and a desktop
-- without the tool installed is not a failed install.
function try_run(program, argv)
    return try { function () os.vrunv(program, argv); return true end } or false
end

function try_output(program, argv)
    return try { function () return os.iorunv(program, argv) end }
end

-- True when the install would otherwise write into someone else's home.
function running_as_root()
    if os.getenv("SUDO_USER") then
        return true
    end
    local uid = try_output("id", {"-u"})
    return uid ~= nil and uid:trim() == "0"
end

function write(file, content)
    os.mkdir(path.directory(file))
    io.writefile(file, content)
    cprint("${green}>>${clear} %s", file)
end

-- Linux refuses to write to a running executable, and the portal service
-- is a long-lived background process running exactly the file an install
-- overwrites. Stopping it costs nothing: it is D-Bus activated, so the next
-- file chooser request starts whatever was just installed.
--
-- The match is anchored on the whole command line so that only the service
-- is hit, never a file manager window someone has open.
function stop_portal_service(bin)
    if not try_run("pkill", {"-x", "-f", bin .. " --portal-service"}) then
        return
    end
    cprint("${green}>>${clear} stopped the running portal service")

    -- the exec stays busy for as long as the kernel takes to reap it
    for _ = 1, 50 do
        os.sleep(100)
        local left = try_output("pgrep", {"-x", "-f", bin .. " --portal-service"})
        if not left or left:trim() == "" then
            return
        end
    end
end

-- Anything else still running the file an install is about to overwrite.
-- Returns a printable list of pids, or nil when the path is free.
function processes_using(bin)
    local pids = try_output("pgrep", {"-f", bin})
    if not pids or pids:trim() == "" then
        return nil
    end
    return pids:trim():gsub("%s+", " ")
end

-- Writes the menu entry, picker entry, D-Bus services and portal
-- registration under `share`, all pointing at the installed binary `bin`.
function install_files(bin, share)
    -- The application menu entry. MimeType only declares that lizaveta
    -- *can* open directories; becoming the default handler is left to
    -- `xmake setup-defaults`.
    --
    -- Icon is a themed name rather than meta/lizaveta.png, which is a wide
    -- banner and would be letterboxed in a menu.
    write(path.join(share, "applications/lizaveta.desktop"), format([[
[Desktop Entry]
Type=Application
Name=Lizaveta
GenericName=File Manager
Comment=Browse and manage files
Exec=%s %%U
Icon=system-file-manager
Terminal=false
Categories=Utility;FileTools;FileManager;
MimeType=inode/directory;
Keywords=files;folders;browse;manager;
StartupNotify=false
]], bin))

    -- The picker entry backs the file-selector scheme and is not something
    -- to launch from a menu, hence NoDisplay.
    --
    -- The Exec first word must be the bare executable path with no quotes:
    -- xdg-open runs `command -v <first word>` to validate the entry and
    -- rejects quoted paths.
    write(path.join(share, "applications/lizaveta-filechooser.desktop"), format([[
[Desktop Entry]
Type=Application
Name=Lizaveta (file picker)
GenericName=Select a file
Comment=Choose a file with the lizaveta file manager
Exec=%s --filechooser %%f
Icon=system-file-manager
Terminal=false
NoDisplay=true
Categories=Utility;FileTools;FileManager;
MimeType=x-scheme-handler/file-selector;
]], bin))

    -- D-Bus activation: starts `lizaveta --portal-service` on demand, the
    -- first time something actually calls the portal backend.
    write(path.join(share, "dbus-1/services/org.freedesktop.impl.portal.desktop.lizaveta.service"),
          format([[
[D-BUS Service]
Name=org.freedesktop.impl.portal.desktop.lizaveta
Exec=%s --portal-service
]], bin))

    -- FileManager1 ("Show in folder" from Firefox's downloads panel and
    -- similar) is an older mechanism unrelated to xdg-desktop-portal, and
    -- the same process answers both. D-Bus activation is per-name, so it
    -- still needs its own service file to be found.
    write(path.join(share, "dbus-1/services/org.freedesktop.FileManager1.service"), format([[
[D-BUS Service]
Name=org.freedesktop.FileManager1
Exec=%s --portal-service
]], bin))

    -- Portal registration. The .portal file name is the portal name that
    -- portals.conf refers to.
    write(path.join(share, "xdg-desktop-portal/portals/lizaveta.portal"), [[
[portal]
DBusName=org.freedesktop.impl.portal.desktop.lizaveta
Interfaces=org.freedesktop.impl.portal.FileChooser
UseIn=*
]])

    try_run("update-desktop-database", {path.join(share, "applications")})
end

local FILECHOOSER_KEY = "org.freedesktop.impl.portal.FileChooser"

local function is_section(line)
    local trimmed = line:trim()
    return trimmed:sub(1, 1) == "[" and trimmed:sub(-1) == "]"
end

local function is_filechooser(line)
    local trimmed = line:trim()
    return trimmed:sub(1, #FILECHOOSER_KEY) == FILECHOOSER_KEY
           and trimmed:sub(#FILECHOOSER_KEY + 1):trim():sub(1, 1) == "="
end

-- Adds the FileChooser preference to an existing portals.conf without
-- disturbing anything else in it.
--
-- xdg-desktop-portal resolves an interface from the section named after
-- XDG_CURRENT_DESKTOP first, and only falls back to [preferred] when that
-- section does not mention the interface. A desktop that ships its own
-- section, as Hyprland and KDE both do, would therefore never see a
-- preference written to [preferred] alone. So the key goes into every
-- section present, and the ScreenCast, Screenshot and GlobalShortcuts
-- lines around it are left exactly as they are.
function portals_conf_with_lizaveta(text)
    local out = {}
    local sections = 0

    local lines = (text and text ~= "") and text:split("\n", {strict = true}) or {}
    for _, line in ipairs(lines) do
        if is_section(line) then
            table.insert(out, line)
            table.insert(out, FILECHOOSER_KEY .. "=lizaveta")
            sections = sections + 1
        elseif not is_filechooser(line) then
            table.insert(out, line)
        end
    end

    if sections == 0 then
        table.insert(out, "[preferred]")
        table.insert(out, FILECHOOSER_KEY .. "=lizaveta")
    end

    local joined = table.concat(out, "\n")
    if joined:sub(-1) ~= "\n" then
        joined = joined .. "\n"
    end
    return joined
end

-- The portal configuration files that matter, in the order
-- xdg-desktop-portal looks for them.
--
-- A <desktop>-portals.conf shadows portals.conf outright rather than
-- merging with it, so writing only the generic file has no effect at all on
-- a desktop that ships its own. Both are updated, which also means the
-- preference survives either file later being removed.
local function portal_conf_files()
    local dir = path.join(config_home(), "xdg-desktop-portal")
    local files = {}

    local desktop = os.getenv("XDG_CURRENT_DESKTOP")
    if desktop and desktop ~= "" then
        -- the variable may list several names; the first is the session
        local first = desktop:split(":", {strict = true})[1]
        if first and first ~= "" then
            local specific = path.join(dir, first:lower() .. "-portals.conf")
            if os.isfile(specific) then
                table.insert(files, specific)
            end
        end
    end

    table.insert(files, path.join(dir, "portals.conf"))
    return files
end

-- Points the FileChooser portal at lizaveta and restarts the portal
-- daemons so they see it. Touches the invoking user's configuration.
function wire_portal()
    for _, conf in ipairs(portal_conf_files()) do
        local existing = os.isfile(conf) and io.readfile(conf) or nil
        if existing and not os.isfile(conf .. ".bak") then
            os.cp(conf, conf .. ".bak")
            cprint("${green}>>${clear} backed up %s to %s.bak", conf, conf)
        end
        write(conf, portals_conf_with_lizaveta(existing))
    end

    -- The file-selector scheme is lizaveta's own and nothing else claims
    -- it, so pointing it at the picker overrides no real choice.
    if try_run("xdg-mime", {"default", "lizaveta-filechooser.desktop",
                            "x-scheme-handler/file-selector"}) then
        cprint("${green}>>${clear} x-scheme-handler/file-selector -> lizaveta-filechooser.desktop")
    end

    -- Restart the portal daemons so they pick up the new .portal file and
    -- portals.conf. Everything here is D-Bus activated, so they come back on
    -- demand; lizaveta's own service only starts the first time a file
    -- picker is actually requested, it is not left running otherwise.
    try_run("pkill", {"-f", "/usr/lib/xdg-desktop-[p]ortal"})
    try_run("pkill", {"-f", "xdg-desktop-portal-[g]tk"})

    -- org.freedesktop.FileManager1 is commonly held by a *running* file
    -- manager rather than being left free for activation, and requesting the
    -- name cannot preempt that. Say so now, instead of leaving the reason
    -- "Show in folder" opens the wrong app to be guessed at.
    local owner = try_output("dbus-send", {"--session", "--print-reply",
                                           "--dest=org.freedesktop.DBus",
                                           "/org/freedesktop/DBus",
                                           "org.freedesktop.DBus.GetNameOwner",
                                           "string:org.freedesktop.FileManager1"})
    if owner then
        cprint("")
        cprint("${yellow}note:${clear} another file manager currently owns org.freedesktop.FileManager1.")
        cprint("      \"Show in folder\" keeps opening that one until it exits. Close it")
        cprint("      once and lizaveta takes over from then on.")
    end
end

function remove_files(share)
    for _, file in ipairs(files()) do
        local full = path.join(share, file)
        if os.isfile(full) then
            os.rm(full)
            cprint("${green}<<${clear} %s", full)
        end
    end
    try_run("update-desktop-database", {path.join(share, "applications")})
end

-- Associations that override choices someone may have made deliberately,
-- which is why they are opt-in rather than part of the install.
function setup_defaults()
    if running_as_root() then
        raise("run this as your normal user, not as root")
    end

    if try_run("xdg-mime", {"default", "lizaveta.desktop", "inode/directory"}) then
        cprint("${green}>>${clear} inode/directory -> lizaveta.desktop")
    end

    -- Firefox only uses the portal file chooser when told to. Patching the
    -- user-level desktop entry overrides the system one without touching
    -- /usr.
    local src = "/usr/share/applications/firefox.desktop"
    local dst = path.join(data_home(), "applications/firefox.desktop")
    if not os.isfile(src) then
        return
    end
    if os.isfile(dst) and io.readfile(dst):find("GTK_USE_PORTAL=1", 1, true) then
        cprint("${green}>>${clear} firefox already routed through the portal")
        return
    end
    os.mkdir(path.directory(dst))
    io.writefile(dst, io.readfile(src):gsub("\nExec=", "\nExec=env GTK_USE_PORTAL=1 "))
    cprint("${green}>>${clear} %s (GTK_USE_PORTAL=1)", dst)
    cprint("   launching firefox from a terminal needs 'export GTK_USE_PORTAL=1' instead")
    try_run("update-desktop-database", {path.directory(dst)})
end

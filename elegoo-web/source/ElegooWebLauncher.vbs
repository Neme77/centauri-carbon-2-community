Option Explicit
Dim sh, fso, base, ps, cmd
Set sh = CreateObject("WScript.Shell")
Set fso = CreateObject("Scripting.FileSystemObject")
base = fso.GetParentFolderName(WScript.ScriptFullName)
ps = fso.BuildPath(base, "ElegooWebGUI.ps1")
cmd = "powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File """ & ps & """"
sh.Run cmd, 0, False

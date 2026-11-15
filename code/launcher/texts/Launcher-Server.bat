@ECHO OFF
REM SerenityJediEngine2027 dedicated server. Built into the launcher (code/launcher/texts/Launcher-Server.bat):
REM START SERVER writes it to %TEMP% and runs it with the game folder as the current folder.
REM The server settings are Launcher-Server.cfg in SerenityJediEngine2027-Assets-1.pk3.
ECHO This batch file starts an SerenityJediEngine dedicated server.
ECHO It will autorestart the server whenever it crashes.
ECHO To stop the restart cycle, just close this window.

ECHO Genuine SerenityJediEngine Combat System OK.

ECHO Starting Server...

ECHO Now Start SerenityJediEngine and Join the Server.
ECHO.
call SerenityJediEngine2027-DED.x86_64 +set fs_game SerenityJediEngine2027 +set dedicated 2 +exec Launcher-Server.cfg

ECHO.
ECHO Restarting Server...
ECHO.
ECHO.Please Wait

ECHO SerenityJediEngine server auto-restarted: >> SerenityJediEngine.txt

DATE /t >> SerenityJediEngine.txt
TIME /t >> SerenityJediEngine.txt

call SerenityJediEngine2027-DED.x86_64 +set fs_game SerenityJediEngine2027 +set dedicated 2 +exec Launcher-Server.cfg

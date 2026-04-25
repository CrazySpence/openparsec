#!/bin/sh
set -e

BOT_NAME="${BOT_NAME:-botman}"
BOT_SERVER="${BOT_SERVER:-enyo.openparsec.com}"

# ExecConsoleFile() uses SYS_fopen which searches gamedata/, packages, then
# the cwd root — it does NOT search cons/. Write bot.con to the cwd root.
cat > /app/openparsec-assets/bot.con <<EOF
name ${BOT_NAME}
clbot.server ${BOT_SERVER}
clbot.start
EOF

# The parsec binary resolves gamedata/, cons/, etc. relative to its working
# directory on Linux. Change into the assets root so it can find them.
cd /app/openparsec-assets

exec /app/parsec -b "$@"

#!/bin/sh
set -e

BOT_NAME="${BOT_NAME:-botman}"
BOT_SERVER="${BOT_SERVER:-enyo.openparsec.com}"

cat > /app/openparsec-assets/cons/bot.con <<EOF
name ${BOT_NAME}
clbot.server ${BOT_SERVER}
clbot.start
EOF

# The parsec binary resolves gamedata/, cons/, etc. relative to its working
# directory on Linux. Change into the assets root so it can find them.
cd /app/openparsec-assets

exec /app/parsec -b "$@"

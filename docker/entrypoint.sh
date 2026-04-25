#!/bin/sh
set -e

BOT_NAME="${BOT_NAME:-botman}"
BOT_SERVER="${BOT_SERVER:-enyo.openparsec.com}"

cat > /app/openparsec-assets/cons/bot.con <<EOF
name ${BOT_NAME}
clbot.server ${BOT_SERVER}
clbot.start
EOF

exec /app/parsec -b "$@"

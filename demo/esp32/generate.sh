# generate header + .c with tables and glyphs
python generate_tables_and_font.py \
  --out arduino_tables --emit-c \
  --font-file /path/to/FreeMonoBold.ttf --font-size 14 \
  --glyph-chars " !?0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"

if [ -f "$HERE/usr/bin/smw_coop.ips" ]; then
    export SNESRECOMP_COOP_IPS="$HERE/usr/bin/smw_coop.ips"
fi
if [ -d "$HERE/usr/bin/lua" ] && [ -w "$ROMDIR" ]; then
    mkdir -p "$ROMDIR/lua"
    for example in "$HERE/usr/bin/lua/"*; do
        target="$ROMDIR/lua/$(basename "$example")"
        if [ ! -e "$target" ]; then cp "$example" "$target"; chmod u+rw "$target"; fi
    done
fi

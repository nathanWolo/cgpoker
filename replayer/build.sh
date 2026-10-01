#!/usr/bin/env bash
# Compile the Java replayer into replayer/build/:
#   referee model classes from the third_party/CodingamePoker submodule (listed in sources.txt)
#   + our own classes / overlays under replayer/src (an overlay replaces the upstream file of the same path)
#   + the no-op slf4j stub under replayer/stub.
# Usage: replayer/build.sh        (then: java -cp replayer/build com.codingame.game.Replayer < input)
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
UP="$HERE/../third_party/CodingamePoker/CodingamePoker/src/main"
OUT="$HERE/build"

if [ ! -f "$UP/java/com/codingame/model/object/board/Board.java" ]; then
  echo "referee sources missing: run 'git submodule update --init third_party/CodingamePoker'" >&2
  exit 1
fi

list="$(mktemp)"
trap 'rm -f "$list"' EXIT
grep -v '^#' "$HERE/sources.txt" | while read -r rel; do
  [ -z "$rel" ] && continue
  if [ -f "$HERE/src/$rel" ]; then echo "$HERE/src/$rel"; else echo "$UP/java/$rel"; fi
done > "$list"
# our own classes (Replayer, PreEq, and any overlay not already listed above)
find "$HERE/src" -name '*.java' | while read -r f; do
  grep -qxF "$f" "$list" || echo "$f"
done >> "$list"
find "$HERE/stub" -name '*.java' >> "$list"

rm -rf "$OUT"
mkdir -p "$OUT"
sed -i 's/.*/"&"/' "$list"                        # quote paths for the javac @argfile
javac -nowarn -encoding UTF-8 -d "$OUT" @"$list"
cp "$UP/resources/message.properties" "$OUT/"   # MessageUtils loads ResourceBundle "message"
echo "built $(find "$OUT" -name '*.class' | wc -l) classes from $(wc -l < "$list") sources into $OUT"

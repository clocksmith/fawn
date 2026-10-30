# usage: physical.sh <src-root> <build-native-dir> <out.tsv>
set -u
SRC=$1; B=$2; OUT=$3
cd $SRC/runtime/c/native
clients="$(cat $(dirname $0)/clients.txt) external-textures optional-formats legacy-api indexed-guard-reuse persistent-cache singleton-layout iosurface-metadata iosurface-shared metal-async-physical"
: > $OUT
for c in $clients; do
  if ! make -s BUILD=$B CC=clang $B/$c > $B/../build-$c.log 2>&1; then printf '%s\tunbuildable\n' $c >> $OUT; continue; fi
  DOE_C_TEST_BACKEND=metal DOE_C_TEST_ADAPTER=physical timeout 60 $B/$c > $B/../run-$c.log 2>&1
  printf '%s\t%s\n' $c $? >> $OUT
done

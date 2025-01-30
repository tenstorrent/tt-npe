read -r -d '' LICTEXT << 'END'
// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC
END

for f in $@; do
	printf "$LICTEXT\n\n" > tmp
	cat $f          >> tmp
	echo $f
	diff tmp $f
	mv tmp $f
done

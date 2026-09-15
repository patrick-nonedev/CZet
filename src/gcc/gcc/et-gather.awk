#  Copyright (C) 2026 Free Software Foundation, Inc.
#  Based on opt-gather.awk.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 3, or (at your option) any
# later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; see the file COPYING3.  If not see
# <http://www.gnu.org/licenses/>.

# This Awk script takes et-static.def and creates a three-field list
# suitable for input into gen-et-static.awk.
#
# Usage: awk -f et-gather.awk et-static.def > outputfile

BEGIN {	numrec = 0 }

# Ignore comments and blank lines
/^[ \t]*(#|$)/  { flag = 0; next }
/^[^ \t]/       { if (flag == 0) {
                    record[++numrec] = $0
		    flag = 1 }
		  else {
		    record[numrec] = record[numrec] SUBSEP $0
	          }
}

END {
    for (i = 1; i <= numrec; i++) {
	print record[i] }
}

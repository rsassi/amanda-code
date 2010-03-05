#! @PERL@
# Copyright (c) 2010 Zmanda, Inc.  All Rights Reserved.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 as published
# by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
# or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
# for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
#
# Contact information: Zmanda Inc., 465 S. Mathilda Ave., Suite 300
# Sunnyvale, CA 94086, USA, or: http://www.zmanda.com

use strict;
use warnings;

my $outfile = $ENV{'INSTALLCHECK_MOCK_PRINTER_OUTPUT'};
die "INSTALLCHECK_MOCK_PRINTER_OUTPUT not defined" unless defined $outfile;

# just copy data
open(my $out, ">", $outfile) or die("Could not open '$outfile'");
while (1) {
    my $data = <STDIN>;
    last unless $data;
    print $out $data;
}
close($out);

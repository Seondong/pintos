# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(hello) begin
hello, world!
(hello) end
EOF
pass;

#!/usr/bin/env perl

use v5.10;
use strict;
use warnings;

my $page = shift or die 'missing page';
my @pages = ();

while (<>) {
	chomp;
	@pages = (@pages, $_);
}

say "<nav>";
my $did = 0;
for (@pages) {
	if (!$did) {
		$did = 1;
	} else {
		print "| ";
	}

	my ($href, $text) = m/^([^\s]*)\s*(.*)$/;
	if ($href eq $page) {
		print "<strong>$text</strong> ";
	} else {
		print "<a href='$href'>$text</a> ";
	}
}

say "</nav>";

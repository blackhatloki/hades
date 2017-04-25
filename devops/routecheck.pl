#!/usr/bin/perl

# routecheck.pl
# check out all routes in routing table and ping gateways
# written by : Glenn Komsky

chop(my $host = `uname -n`);
my @print_order = ();

# not using egrep for maximum compatibility
my @gw_if = `netstat -rn |awk '{print \$2,\$8}' | grep -v IP |grep -v Gateway|grep -v bond0 |grep -v 0.0.0.0|sort -u`;

sPingGateways (@gw_if);

sub sPingGateways {
	my @gw_if = @_;

	for (@gw_if) {
		chomp;
		my ($gw,$if) = split(/\s+/,$_,2);
		
		if ($gw) {
			# Fork each ping process so all interfaces are tested simultaneously and reported on
			my $pid = fork();
			if ($pid) {
				# parent
				push(@childs, $pid);
			} elsif ($pid == 0) {
				# child
				eval {
					local $SIG{ALRM} = sub { die "Timeout\n"; };
					alarm(30);
					@ping_ret = `ping -I $if $gw -c 3`;
					alarm(0); # turn off the alarm clock
				};
				# if there is an error
				if (($@) || ($#ping_ret < 0)) {
					print "$host,$cur_if, Ping HANGING - Timed Out and moving on.\n";
				} else {
					sResults($if,$gw,@ping_ret);
				};

				exit (0);
			} else {
				die "couldn't fork: $!\n";
                        };

		};
	};

	foreach (@childs) {
		waitpid($_,0);
	};

};

sub sResults {
        my $cur_if = shift;
        my $cur_gw = shift;
        my @ping_ret = @_;

        my $transmitted = "";
        my $received = "";
        my $errors = "";
        my $loss = "";
        my $time = "";
        my $min = "";
        my $avg = "";
        my $max = "";
        my $mdev = "";
        my @details = ();

        for my $ping_ret (@ping_ret) {
                chomp($ping_ret);
                if ($ping_ret =~ /transmitted/) {
                        $ping_ret =~ s/\,\s+/,/g;
                        @details = split(/\,/,$ping_ret);
                        if ($#details == 4) {
                                ($transmitted,$j2) = split(/\s+/,$details[0],2);
                                ($received,$j3) = split(/\s+/,$details[1],2);
                                ($errors,$j4) = split(/\s+/,$details[2],2);
                                ($loss,$j5) = split(/\s+/,$details[3],2);
                                $loss =~ s/\%//g;
                                ($j6,$time) = split(/\s+/,$details[4],2);
                        } else {
                                ($transmitted,$j2) = split(/\s+/,$details[0],2);
                                ($received,$j3) = split(/\s+/,$details[1],2);
                                ($loss,$j5) = split(/\s+/,$details[2],2);
                                ($j6,$time) = split(/\s+/,$details[3],2);
                                $loss =~ s/\%//g;
                                $errors = 0;
                        };
                        if ($debug) {
                                print "DEBUG: $ping_ret\n";
                        };
                } elsif ($ping_ret =~ /min\/avg/) {
                        my ($label,$label2,$eq,$values,$remainder) = split(/\s+/,$ping_ret,5);
                        ($min,$avg,$max,$mdev) = split(/\//,$values,4);
                        if ($debug) {
                                print "DEBUG: $ping_ret\n";
                        };
                };
        };

        if ($transmitted != $received) {
                print "FAIL: $host,$cur_if,GW($cur_gw),$transmitted pkts transmitted,$received received,$errors errors,$loss\% pkt loss,time $time\n";
        } elsif ($errors) {
                print "WARNING: $host,$cur_if,GW($cur_gw),$transmitted pkts transmitted,$received received,$errors errors,$loss\% pkt loss,time $time\n";
        } elsif ($loss == 100) {
                print "FAIL: $host,$cur_if,GW($cur_gw),$transmitted pkts transmitted,$received received,$errors errors,$loss\% pkt loss,time $time\n";
        } elsif ($loss) {
                print "WARNING: $host,$cur_if,GW($cur_gw),$transmitted pkts transmitted,$received received,$errors errors,$loss\% pkt loss,time $time\n";
        } else {
                print "PASS: $host,$cur_if,GW($cur_gw),$transmitted pkts transmitted,$received received,$errors errors,$loss\% pkt loss,time $time\n";
        };

};


#!/usr/bin/perl
use strict;
use Getopt::Std;
use Device::SerialPort;
use Time::HiRes;
use bytes;
use Term::ANSIColor qw(:constants);

# flush immediately
$| = 1;

# defaults
my $bufsize = 512;  # read/write buffer size [bytes]
my $device = "/dev/ttyUSB0";
my $fn = "data.rom";
my $etype = 0; # unknown EPROM

# get options
my %options=();
getopts("f:d:h",\%options);
if ((defined $options{h}) || ($#ARGV < 0) || ($#ARGV > 0))
{
	# print help
	print "\nUsage: $0 [options] <command>\n";
	print "\nCommands:\n", RED, "  read", RESET, "  - Read the full EPROM\n";
	print RED, "  erase", RESET, " - Electrically erase a W27Cxxx EPROM\n";
	print RED, "  write", RESET, " - Write the full EPROM\n";
	print "\nOptions:\n";
	print "  -f filename - Filename for read/write (default: ${fn})\n";
	print "  -d device - Arduino serial port device (default: ${device})\n";
	print "  -t type - EPROM type (0=autodetect,1=W27C512, 2=W27C040, 3=W27C257)\n";
	print "\n";
	exit 0;
}
if (defined($options{f}))
{
	$fn = $options{f};
}
if (defined($options{d}))
{
	$device = $options{d};
}
if (defined($options{t}))
{
	$etype = $options{t};
}

# create and configure port
my $port = Device::SerialPort->new($device) || die "Could not open serial port: $!\n";
$port->baudrate(115200);
$port->databits(8);
$port->parity("none");
$port->stopbits(1);
$port->datatype('raw');
$port->read_char_time(0);     # don't wait for each character
$port->read_const_time(100); # 0.1 second per unfulfilled "read" call

# wait for ARDUINO sketch identifier
print "Connecting to FlipBits Board...";
my $id = "";
while (!($id =~ /FlipBits 1.0 - ready/))
{
	$id .= $port->lookfor();
}
print GREEN, "  ✔\n", RESET;

# set/identify the EPROM
if ($etype == 0)
{
	$port->write("i:");
}
else
{
	$port->write("t${etype}:");
}
# parse the response
while (!($id =~ /.*EPROM mid (\d+) pid (\d+) type (\d+) name (.*?) size (\d+)/))
{
	$id .= $port->lookfor();
}
$id =~ /EPROM mid (\d+) pid (\d+) type (\d+) name (.*?) size (\d+)/;
if (($4 eq "Unknown") || ($5 == 0))
{
	print RED, "No valid EPROM detected: $4 $5\n", RESET;
	exit 1;
}
my $esize = $5;
print "EPROM ";
print GREEN, "$4 ", RESET;
print "size ";
print YELLOW, "$esize", RESET;
print " detected ";
print GREEN, "✔\n", RESET;

# execute commands
my $cmd = $ARGV[0];
if ($cmd eq "read")
{
	# read the EPROM
	my $rom = readEPROM(0, "");

	# write data to the output file
	open(ROMFILE, ">$fn") || die "Cannot open file $fn: $!\n";
	print ROMFILE $rom;
	close(ROMFILE);
	print "\nWritten EPROM content to ";
	print YELLOW, "$fn", RESET;
	print ".\n"
}
elsif ($cmd eq "write")
{
	# read the EPROM
	my $wrom = writeEPROM();

	# verify the EPROM content
	readEPROM(2, $wrom);
}
elsif ($cmd eq "erase")
{
	# erase the EPROM
	eraseEPROM();
}
else
{
	# unknown command
	print "Unknown command: $cmd\n";
	exit 1;
}

print "\n";
# done
exit 0;


#---------------------------------------------------------------------------------
sub eraseEPROM
{
	# erase the EPROM
	printProgress("Erasing", 0, $esize, 40, 1);
	$port->write("e:");

	# wait for erase done signal
	my $id = "";
	my $done = 0;
	while (!$done)
	{
		if ($id =~ /ERASE COMPLETE/)
		{
			printProgress("Erasing  ", 1.0, $esize, 40, 1);
			print GREEN, "  ✔\n", RESET;
			$done = 2;
		}
		elsif ($id =~ /ERASE FAILED/)
		{
			printProgress("Erasing  ", 1.0, $esize, 40, 2);
			print RED, "  🕱\n", RESET;
			$done = 1;
		}
		else
		{
			$id .= $port->lookfor();
		}
	}

	if ($done == 2)
	{
		readEPROM(1, "");
	}
}

#---------------------------------------------------------------------------------
sub readEPROM
{
	my $verify = shift;
	my $vrom = shift;
	my @verBytes = split //, $vrom;
	my $label = ($verify) ? "Verifying" : "Reading  ";
	my $rom = "";

	# flush serial
	my $da = 1;
	while ($da)
	{
		($da, my $dummy) = $port->read(255);
	}

	# initiate EPROM read mode
	$port->write("r:");

	# receive ROM data, it is sent in $bufsize chunks
	my $timeout = 20;
	my $bytes = 0;
	my $startseen = 0;
	my $lastBytes = 0;
	READ: while (($timeout--) && ($bytes < $esize))
	{
		my $count = 1;
		my $saw;
		while ($count)
		{
			# read up to 255 bytes
			($count, $saw) = $port->read(255);
			$rom .= $saw;

			# check whether all data is 0xff (empty)
			if ($verify == 1)
			{
				foreach my $byte (split //, $saw)
				{
					if (unpack("C", $byte) != 0xff)
					{
						# non empty byte seen -> abort
						print RED, " 🕱 NOT EMPTY\n", RESET;
						last READ;
					}
				}
			}
			# verify data against original
			elsif ($verify == 2)
			{
				my $bc = $bytes;
				foreach my $byte (split //, $saw)
				{
					if ($byte != $verBytes[$bc])
					{
						my $diffPos = ($bytes + $bc);
						print RED, " 🕱 Diff at ${diffPos}!\n", RESET;
					}
					$bc++;
				}
			}

			$bytes += $count;

			# draw a progress bar
			if (($bytes - $lastBytes) > $bufsize)
			{
				my $p = ($bytes / $esize);
				printProgress($label, $p, $bytes, 40, 0);
				$lastBytes = $bytes;
			}
		}
	}
	my $p = ($bytes / $esize);
	printProgress($label, $p, $bytes, 40, 0);

	print GREEN, "  ✔\n", RESET;
	return $rom;
}

#---------------------------------------------------------------------------------
sub writeEPROM
{
	my $rom = "";

	# read the rom file
	open(ROMFILE, "<$fn") || die "Cannot open file $fn: $!\n";
	binmode(ROMFILE);
	my $romBytes = read(ROMFILE,$rom,$esize);
	if ($romBytes != $esize)
	{
		die "ROM file too small: $romBytes - $esize\n";
	}
	print GREEN, "$romBytes", RESET;
	print "b read from ";
	print YELLOW, "$fn", RESET;
	print "\n";
	close(ROMFILE);

	# initiate EPROM write mode
	$port->write("w:");

	# wait for write mode ready signal
	my $id = "";
	while (!($id =~ /WRITE READY/))
	{
		$id .= $port->lookfor();
 	}

	# send all data
	my $count = 0;
	my $rx = "";
	my $ready = 0;
	for (my $b=0; $b<$esize; $b+=$bufsize)
	{
		# send the ROM bytes in [bufsize] chunks
		my $db = substr($rom, $b, $bufsize);
		$port->write($db);

		# wait for acknowledge
		my $timeout = 100;
		while ($timeout--)
		{
			($count, $rx) = $port->read(255);
			if ($rx =~ /READY/)
			{
				$timeout = 0;
			    $ready = 1;

				# draw a progress bar
				my $p = (($b+$bufsize) / $esize);
				printProgress("Writing  ", $p, $b+$bufsize, 40, 2);
			}
		}
		if ($ready == 0)
		{
			print "CONNECTION TIMEOUT!\n";
			$b = $esize;
		}
	}

	# wait for write done signal
	if ($ready)
	{
		my $done = 0;
		while ($done == 0)
		{
			if ($rx =~ /WRITE COMPLETE/)
			{
				printProgress("Writing  ", 1.0, $esize, 40, 2);
				print GREEN, "  ✔\n", RESET;
				$done = 2;
			}
			elsif ($id =~ /WRITE FAILED/)
			{
				printProgress("Writing  ", 1.0, $esize, 40, 2);
				print RED, "  🕱\n", RESET;
				$done = 1;
			}
			else
			{
				$id .= $port->lookfor();
			}
		}
	}
	else
	{
		print "Write failed 🕱!\n";
	}
	return $rom;
}

#---------------------------------------------------------------------------------
sub printProgress
{
	my $t = shift;
	my $p = shift;
	my $bytes = shift;
	my $bw = shift;
	my $color = shift;
	my $bw = 40;

	my $pb = int($p*$bw);
	print "\r$t ";
	for (my $i=0; $i<$pb; $i++)
	{
		if ($color == 0)
		{
			print GREEN, "▰", RESET;
		}
		elsif ($color == 1)
		{
			print YELLOW, "▰", RESET;
		}
		else
		{
			print RED, "▰", RESET;
		}
	}
	if (($p*$bw-$pb) >= 0.5)
	{
		print YELLOW, "▰", RESET;
		$pb++;
	}
	for (my $i=$pb; $i<$bw; $i++)
	{
		print "▱";
	}
	printf " %3.0f%% ${bytes}b", $p*100;
}

////#include <filesystem>
//#include <QCommandLineParser>
//#include <QCoreApplication>
//#include <QDebug>
//#include <QElapsedTimer>
//#include <chrono>
//#include <filesystem>
//#include <fmt/format.h>
//#include <fstream>
//#include <iomanip>
//#include <iostream>
//#include <unistd.h>

//namespace ch = std::chrono;
//namespace fs = std::filesystem;

//template <typename TP>
//auto as_system_clock(const TP& tp) {
//	auto sctp = ch::time_point_cast<ch::system_clock::duration>(tp - TP::clock::now() + ch::system_clock::now());
//	return sctp;
//}

////Poor Man iostat for the device where the operation is in progress
//auto await() {
//	//	FILE *fp;
//	//	struct io_stats sdev;
//	//	int i;
//	//	unsigned int ios_pgr, tot_ticks, rq_ticks, wr_ticks, dc_ticks, fl_ticks;
//	//	unsigned long rd_ios, rd_merges_or_rd_sec, wr_ios, wr_merges;
//	//	unsigned long rd_sec_or_wr_ios, wr_sec, rd_ticks_or_wr_sec;
//	//	unsigned long dc_ios, dc_merges, dc_sec, fl_ios;

//	//	/* Try to read given stat file */
//	//	if ((fp = fopen(filename, "r")) == NULL)
//	//		return -1;

//	//	i = fscanf(fp, "%lu %lu %lu %lu %lu %lu %lu %u %u %u %u %lu %lu %lu %u %lu %u",
//	//		   &rd_ios, &rd_merges_or_rd_sec, &rd_sec_or_wr_ios, &rd_ticks_or_wr_sec,
//	//		   &wr_ios, &wr_merges, &wr_sec, &wr_ticks, &ios_pgr, &tot_ticks, &rq_ticks,
//	//		   &dc_ios, &dc_merges, &dc_sec, &dc_ticks,
//	//		   &fl_ios, &fl_ticks);
//}

//int main(int argc, char* argv[]) {
//	std::setlocale(LC_ALL, "C");

//	QCoreApplication application(argc, argv);
//	QCoreApplication::setApplicationName("deleterV1");
//	QCoreApplication::setApplicationVersion("0.01");

//	QCommandLineParser parser;
//	parser.addHelpOption();
//	parser.addVersionOption();

//	parser.addOption({{"p", "path"}, "from where to start", "string"});
//	parser.addOption({{"f", "onlyFile"}, "remove only file"});
//	parser.addOption({{"ma", "maxAge"}, "how old stuff can be (days), default 1", "int", "1"});
//	parser.addOption({{"r", "remove"}, "enable the removal stuff, else it will only print what is going to remove"});
//	parser.addOption({{"s", "spam"}, "tell us how good you are in deleting file, default 10000", "int", "10000"});
//	parser.addOption({{"d", "dutyCicle"}, "deleting milion of files kills the hard drive, and all the rest will suffer, % of time spend processing data, default 50", "int", "50"});
//	// Process the actual command line arguments given by the user
//	parser.process(application);

//	if (!parser.isSet("path")) {
//		qWarning() << "Where is the path ?";
//		return 1;
//	}

//	auto   day       = parser.value("maxAge").toInt();
//	auto   maxAge    = ch::system_clock::now() - ch::days(day);
//	bool   remove    = parser.isSet("remove");
//	bool   onlyFile  = parser.isSet("onlyFile");
//	uint   spam      = parser.value("spam").toUInt();
//	double dutyCicle = parser.value("dutyCicle").toDouble() / 100;
//	uint   deleted   = 0;
//	uint   evaluated = 0;

//	quint64       busyTime = 0;
//	QElapsedTimer totalTimer, splitTimer;
//	totalTimer.start();
//	splitTimer.start();

//	fs::path path = parser.value("path").toStdString();
//	for (auto& p : fs::recursive_directory_iterator(path)) {
//		//This Duty cicle thing is super easy to do, correct fix would be to detect for the current drive the %util like in iostat, and keep lower than a certain level...
//		busyTime += splitTimer.nsecsElapsed();
//		auto totalTime = totalTimer.nsecsElapsed();
//		if (busyTime > totalTime * dutyCicle) {
//			auto sleep4 = -(totalTime * dutyCicle - busyTime) / 1000;
//			if (sleep4 > 1E6) {
//				fmt::print("Pausing for {:>3.0} to help disk catch up (total: {:>12.4e} active: {:>12.4e}\n", sleep4 / 1E6, (double)totalTime, (double)busyTime);
//				usleep(sleep4);
//			}
//		}
//		splitTimer.restart();
//		evaluated++;
//		if ((evaluated % spam) == 0) {
//			fmt::print("{:>10} {:>7.2e}\n", "evaluated", (double)evaluated);
//		}
//		auto last  = as_system_clock(last_write_time(p));
//		bool isOld = last < maxAge;
//		if (remove) {
//			if (isOld) {
//				if (!onlyFile && p.is_directory()) {
//					continue;
//				}
//				if (last < maxAge) {
//					deleted++;
//					fs::remove(p);
//					if ((deleted % spam) == 0) {
//						fmt::print("{:>10} {:>7.2e}\n", "deleted", (double)deleted);
//					}
//				}
//			}
//		} else {
//			auto local = ch::system_clock::to_time_t(last);
//			std::cout << std::put_time(localtime(&local), "%c") << p;
//			if (isOld) {
//				std::cout << " ---> DELETE";
//			}
//			std::cout << "\n";
//		}
//	}
//	std::cout << "deleted " << deleted << "\n";
//}

#include <fcntl.h>
#include <libgen.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <unistd.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

#include <fmt/format.h>

struct Ios {
	//double has 53bit / ~16 digit, the largest exact integral value is 2^53-1, or 9007199254740991
	double tick      = 0;
	double operation = 0;
	double merges    = 0;
	double sectors   = 0;
};
struct Iostat {
	Ios read;
	Ios write;
};

using namespace std;
Iostat getDeviceStats(string dev) {
	auto          path = "/sys/block/" + dev + "/stat";
	string        line;
	std::ifstream file(path);
	std::getline(file, line);
	std::istringstream iss(line);
	/*
https://www.kernel.org/doc/Documentation/block/stat.txt

Name            units         description
----            -----         -----------
read I/Os       requests      number of read I/Os processed
read merges     requests      number of read I/Os merged with in-queue I/O
read sectors    sectors       number of sectors read
read ticks      milliseconds  total wait time for read requests
write I/Os      requests      number of write I/Os processed
write merges    requests      number of write I/Os merged with in-queue I/O
write sectors   sectors       number of sectors written
write ticks     milliseconds  total wait time for write requests
in_flight       requests      number of I/Os currently in flight
io_ticks        milliseconds  total time this block device has been active
time_in_queue   milliseconds  total wait time for all requests
discard I/Os    requests      number of discard I/Os processed
discard merges  requests      number of discard I/Os merged with in-queue I/O
discard sectors sectors       number of sectors discarded
discard ticks   milliseconds  total wait time for discard requests
*/
	Iostat io;
	if (!(iss >> io.read.operation >> io.read.merges >> io.read.sectors >> io.read.tick >> io.write.operation >> io.write.merges >> io.write.sectors >> io.write.tick)) {
		cerr << "Error reading " << line << " from " << path << "\n";
		exit(EXIT_FAILURE);
	}
	return io;
}
std::string get_device(char* name) {
	struct stat fs;

	if (stat(name, &fs) < 0) {
		cerr << name << ": No such file or directory\n";
		exit(EXIT_FAILURE);
	}

	int min = minor(fs.st_dev);
	int maj = major(fs.st_dev);

	//discard first two lines, really no idea how to read that in a nice struct and not pass via a file -.-
	std::ifstream infile("/proc/partitions");
	std::string   line;
	int           lineNr = 0;
	while (std::getline(infile, line)) {
		lineNr++;
		if (lineNr < 3) {
			continue;
		}
		std::istringstream iss(line);
		int                a, b, c;
		std::string        device;
		if (!(iss >> a >> b >> c >> device)) {
			break;
		} // error
		//this is to get the partition
		//		if (a == maj && b == min) {
		//			return device;
		//		}
		//but we want the device
		if (a == maj && b == 0) {
			return device;
		}
	}
	cerr << "No suitable device found ??? \n";
	exit(EXIT_FAILURE);
}

void delta1(Iostat neu, Iostat old) {
	Iostat delta;
	delta.read.tick       = neu.read.tick - old.read.tick;
	delta.read.operation  = neu.read.operation - old.read.operation;
	delta.write.tick      = neu.write.tick - old.write.tick;
	delta.write.operation = neu.write.operation - old.write.operation;
	auto readWait         = delta.read.tick / delta.read.operation;
	auto writeWait        = delta.write.tick / delta.write.operation;
	fmt::print("r_wait {}	w_wait{} \n", readWait, writeWait);
}

int main(int argc, char** argv) {

	if (argc != 2) {
		cerr << "Usage:\n "s + basename(argv[0]) + " FILE OR DIRECTORY...\n";
		return -1;
	}
	auto device = get_device(argv[1]);
	std::cout << "/dev/" << device << "\n";
	auto old = getDeviceStats(device);
	while (true) {
		sleep(1);
		auto neu = getDeviceStats(device);
		delta1(neu, old);
		old = neu;
	}

	return 0;
}

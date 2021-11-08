#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <chrono>
#include <filesystem>
#include <fmt/format.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <unistd.h>

#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>

#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <thread>

namespace ch = std::chrono;
namespace fs = std::filesystem;
using namespace std;

template <typename TP>
/**
 * @brief as_system_clock will convert a clock (like the filesystem one) to the "normal" one, 
 * because fs have weird clock, for some reason
 * @param tp
 */
auto as_system_clock(const TP& tp) {
	auto sctp = ch::time_point_cast<ch::system_clock::duration>(tp - TP::clock::now() + ch::system_clock::now());
	return sctp;
}

struct Ios {
	uint64_t tick      = 0;
	uint64_t operation = 0;
	uint64_t merges    = 0;
	uint64_t sectors   = 0;
};
struct Iostat {
	Ios read;
	Ios write;
};
//Shared state do not require atomic<Iostat> as x86 read/write are torn free for <= 64bit type
Iostat delta;

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
std::string get_device(const char* name) {
	struct stat fs;

	if (stat(name, &fs) < 0) {
		cerr << name << ": No such file or directory\n";
		exit(EXIT_FAILURE);
	}

	int min = minor(fs.st_dev);
	int maj = major(fs.st_dev);

	fmt::print("File {} resides in {}:{}\n", name, maj, min);

	//discard first two lines, really no idea how to read that in a nice struct and not pass via a file -.-
	std::ifstream infile("/proc/partitions");
	std::string   line;
	int           lineNr = 0;
	while (std::getline(infile, line)) {
		lineNr++;
		if (lineNr < 3) {
			continue;
		}

		std::regex self_regex(R"(\s*(\d*)\s*(\d*)\s*\d*\s*(\w*))");

		std::smatch match;
		if (!std::regex_match(line, match, self_regex)) {
			cerr << "Invalid line: " << line << "\n";
		}
		auto a      = stoi(match[1].str());
		auto b      = stoi(match[2].str());
		auto device = match[3].str();
		//		std::cout << line << '\n';
		//		for (size_t i = 0; i < match.size(); ++i) {
		//			std::ssub_match sub_match = match[i];
		//			std::string     piece     = sub_match.str();
		//			std::cout << "  submatch " << i << ": " << piece << '\n';
		//		}

		//for some reason this is failig in s8-.-
		//		std::istringstream iss(line);
		//		int                a, b, c;
		//		std::string        device;
		//		if (!(iss >> a >> b >> c >> device)) {
		//			break;
		//		}

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
	return std::string();
}

//This will erase the last line
void eraseLine(FILE* file = stdout) {
	fprintf(file, "\033[A\33[2KT\r");
}
double deNaN(double val) {
	if (isnan(val)) {
		return 0;
	}
	return val;
}
[[nodiscard]] Iostat delta1(Iostat neu, Iostat old) {
	Iostat delta;
	delta.read.tick       = neu.read.tick - old.read.tick;
	delta.read.operation  = neu.read.operation - old.read.operation;
	delta.write.tick      = neu.write.tick - old.write.tick;
	delta.write.operation = neu.write.operation - old.write.operation;
	return delta;
}

void busyMeter(string path, double sleepUS) {
	auto device = get_device(path.c_str());
	std::cout << "/dev/" << device << "\n";
	auto old = getDeviceStats(device);
	while (true) {
		usleep(sleepUS);
		auto neu = getDeviceStats(device);
		delta    = delta1(neu, old);
		old      = neu;
		//		auto readWait  = deNaN((double)delta.read.tick / delta.read.operation);
		//		auto writeWait = deNaN((double)delta.write.tick / delta.write.operation);
		//		auto util      = (double)(delta.write.tick + delta.read.tick) / (sleepUS * 100.0);
		//		fmt::print("r_wait: {:5.2f}ms	w_wait: {:5.2f}ms	usage: {:5.2f}%\n", readWait, writeWait, util);
	}
}

struct DeleteConf {
	std::string                  path;
	ch::system_clock::time_point maxAge;
	double                       dutyCicle;
	uint                         util;
	bool                         remove;
	bool                         onlyFile;
	uint                         spam;
};

uint deleteIN(DeleteConf conf) {
	const double  diskUsageSleep = 1e5;
	uint          deleted        = 0;
	uint          evaluated      = 0;
	QElapsedTimer totalTimer, splitTimer;
	totalTimer.start();
	splitTimer.start();
	quint64      busyTime = 0;
	std::jthread busyMeterThread(busyMeter, conf.path, diskUsageSleep);
	for (auto& p : fs::recursive_directory_iterator(conf.path)) {
		//This Duty cicle thing is super easy to do, can be helpfull ...
		busyTime += splitTimer.nsecsElapsed();
		auto totalTime = totalTimer.nsecsElapsed();
		if (busyTime > totalTime * conf.dutyCicle) {
			auto sleep4 = -(totalTime * conf.dutyCicle - busyTime) / 1000;
			if (sleep4 > 1E3) {
				if (conf.spam) {
					fmt::print("Pausing for {:>3.0}ms to help disk catch up (total: {:>12.4e} active: {:>12.4e}\n", sleep4 / 1E3, (double)totalTime, (double)busyTime);
				}
				usleep(sleep4);
				if (conf.spam) {
					eraseLine();
				}
			}
		}

		if (auto busyP = (delta.write.tick + delta.read.tick) / 10.0; busyP > conf.util) {
			fmt::print("Pausing for {}ms to help disk catch up (util: {:>5.2f}%)\n", diskUsageSleep * 0.003, busyP);
			usleep(diskUsageSleep * 3);
			eraseLine();
		}
		splitTimer.restart();
		evaluated++;
		if ((evaluated % conf.spam) == 0) {
			fmt::print("{:>10} {:>7.2e}\n", "evaluated", (double)evaluated);
		}
		//TODO shall we delete empty folder ?
		if (p.is_directory()) {
			continue;
		}
		auto last  = as_system_clock(last_write_time(p));
		bool isOld = last < conf.maxAge;
		if (conf.remove) {
			if (isOld) {
				if (last < conf.maxAge) {
					deleted++;
					fs::remove(p);
					if ((deleted % conf.spam) == 0) {
						fmt::print("{:>10} {:>7.2e}\n", "deleted", (double)deleted);
					}
				}
			}
		} else {
			auto local = ch::system_clock::to_time_t(last);
			std::cout << std::put_time(localtime(&local), "%c") << p;
			if (isOld) {
				std::cout << " ---> DELETE";
			}
			std::cout << "\n";
		}
	}
	return deleted;
}

int main(int argc, char* argv[]) {
	std::setlocale(LC_ALL, "C");

	QCoreApplication application(argc, argv);
	QCoreApplication::setApplicationName("deleterV1");
	QCoreApplication::setApplicationVersion("0.01");

	QCommandLineParser parser;
	parser.addHelpOption();
	parser.addVersionOption();

	parser.addOption({{"p", "path"}, "from where to start", "string"});
	parser.addOption({{"ma", "maxAge"}, "how old stuff can be (days), default 1", "int", "1"});
	parser.addOption({{"r", "remove"}, "enable the removal stuff, else it will only print what is going to remove"});
	parser.addOption({{"s", "spam"}, "tell us how good you are in deleting file, default 10000", "int", "10000"});
	parser.addOption({{"d", "dutyCicle"}, "deleting milion of files kills the hard drive, and all the rest will suffer, % of time spend processing data, default 50", "int", "50"});
	parser.addOption({{"u", "util"}, R"(
A potentially better way to rate limit, avoid DISK use this much time (read + write) (iostat report average of the two), in %, default 30
This value is due to global usage, so it takes into account load from other factor, therefore using only "free time"
This method is not 100% valid,as parallel processing system like NVME can perform multiple task at once. So they can have a 400% usage
)",
	                  "int",
	                  "30"});
	// Process the actual command line arguments given by the user
	parser.process(application);

	if (!parser.isSet("path")) {
		qWarning() << "Where is the path ?";
		return 1;
	}

	DeleteConf conf;
	auto       day = parser.value("maxAge").toInt();
	conf.maxAge    = ch::system_clock::now() - ch::days(day);
	conf.remove    = parser.isSet("remove");
	conf.spam      = parser.value("spam").toUInt();
	conf.dutyCicle = parser.value("dutyCicle").toDouble() / 100;
	conf.util      = parser.value("util").toUInt();

	auto path = parser.value("path").toStdString();

	auto deleted = deleteIN(conf);

	std::cout << "deleted " << deleted << "\n";
}

//#include <stdio.h>
//#include <unistd.h>
//int main(){
//	int i = 3;
//	fprintf(stdout, "\nText to keep\n");
//	fprintf(stdout, "Text to erase****************************\n");
//	while(i > 0) { // 3 second countdown
//		fprintf(stdout, "\033[A\33[2KT\rT minus %d seconds...\n", i);
//		i--;
//		sleep(1);
//	}
//}

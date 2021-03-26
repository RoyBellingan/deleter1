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

//TODO
//Forse è possibile tramite https://man7.org/linux/man-pages/man2/fstatfs.2.html ottenere lo fs e poi il device anche per nvme e altri ? la tecnica del minor = 0 funge solo con gli sdx
namespace ch = std::chrono;
namespace fs = std::filesystem;
using namespace std;

template <typename TP>
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
//Shared state do not require atomic<Iostat> as x86 read/write are torn free for < 64bit type
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
		if (device.substr(0, 2) == "md") {
			//mdadm devices are in a single major, and different raid group are considered partition , which is correct btw...
			//The problem is that a mdamd device has no concept of wait time and load (even iostat report them always as zero)
		} else {
			if (a == maj && b == 0) {
				return device;
			}
		}
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
	}
	cerr << "No suitable device found ??? Try to use -u 0 to disable disk usage monitoring, and rely only on duty cycle % with -d \n";
	exit(EXIT_FAILURE);
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

void busyMeter(std::stop_token stop_token, string path, double sleepUS) {
	auto device = get_device(path.c_str());
	std::cout << "/dev/" << device << "\n";
	auto old = getDeviceStats(device);
	while (true) {
		if (stop_token.stop_requested()) {
			return;
		}
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

int main(int argc, char* argv[]) {
	try {

		std::setlocale(LC_ALL, "C");
		double diskUsageSleep = 1e5;

		QCoreApplication application(argc, argv);
		QCoreApplication::setApplicationName("deleterV1");
		QCoreApplication::setApplicationVersion("0.01");

		QCommandLineParser parser;
		parser.addHelpOption();
		parser.addVersionOption();

		parser.addOption({{"p", "path"}, "from where to start", "string"});
		parser.addOption({{"m", "maxAge"}, "how old stuff can be (days), default 1", "int", "1"});
		parser.addOption({{"r", "remove"}, "enable the removal stuff, else it will only print what is going to remove"});
		parser.addOption({{"s", "spam"}, "tell us how good you are in deleting file, default 10000", "int", "10000"});
		parser.addOption({{"d", "dutyCicle"}, "deleting milion of files kills the hard drive, and all the rest will suffer, % of time spend processing data, default 50", "int", "50"});
		parser.addOption({{"u", "util"}, R"(
A potentially better way to rate limit, avoid DISK use this much time (read + write) (iostat report average of the two), in %, default 30
This value is due to global usage, so it takes into account load from other factor, therefore using only "free time"
This method is not 100% valid,as parallel processing system like NVME can perform multiple task at once. So they can have a 400% usage
Is not very easy to unroll where a file REALLY belong (partiont, raid -> multiple disc ecc), so if this value is 0 mean do not check
)",
		                  "int",
		                  "30"});

		// Process the actual command line arguments given by the user
		parser.process(application);

		if (!parser.isSet("path")) {
			qWarning() << "Where is the path ?";
			return 1;
		}

		auto   day       = parser.value("maxAge").toInt();
		auto   maxAge    = ch::system_clock::now() - ch::days(day);
		bool   remove    = parser.isSet("remove");
		uint   spam      = parser.value("spam").toUInt();
		double dutyCicle = parser.value("dutyCicle").toDouble() / 100;
		uint   util      = parser.value("util").toUInt();
		uint   deleted   = 0;
		uint   evaluated = 0;

		quint64       busyTime = 0;
		QElapsedTimer totalTimer, splitTimer;
		totalTimer.start();
		splitTimer.start();

		auto          path            = parser.value("path").toStdString();
		std::jthread* busyMeterThread = nullptr;
		if (util) {
			busyMeterThread = new std::jthread(busyMeter, path, diskUsageSleep);
		}

		for (auto& p : fs::recursive_directory_iterator(path)) {
			//This Duty cicle thing is super easy to do, can be helpfull ...
			busyTime += splitTimer.nsecsElapsed();
			auto totalTime = totalTimer.nsecsElapsed();
			if (busyTime > totalTime * dutyCicle) {
				auto sleep4 = -(totalTime * dutyCicle - busyTime) / 1000;
				if (sleep4 > 1E6) {
					fmt::print("Pausing for {:>3.0} to help disk catch up (total: {:>12.4e} active: {:>12.4e}\n", sleep4 / 1E6, (double)totalTime, (double)busyTime);
					usleep(sleep4);
					eraseLine();
				}
			}

			if (util) {
				if (auto busyP = (delta.write.tick + delta.read.tick) / 10.0; busyP > util) {
					fmt::print("Pausing for {}ms to help disk catch up (util: {:>5.2f}%)\n", diskUsageSleep * 0.003, busyP);
					usleep(diskUsageSleep * 3);
					eraseLine();
				}
			}

			splitTimer.restart();
			evaluated++;
			if ((evaluated % spam) == 0) {
				fmt::print("{:>10} {:>7.2e}\n", "evaluated", (double)evaluated);
			}
			if (p.is_directory()) {
				continue;
			}
			if (p.is_symlink() && !p.exists()) {
				//if the symlink target do not exist, it will fail fetchint the last last_write_time time
				deleted++;
				fs::remove(p);
			}
			auto last  = as_system_clock(last_write_time(p));
			bool isOld = last < maxAge;
			if (remove) {
				if (isOld) {
					if (last < maxAge) {
						deleted++;
						fs::remove(p);
						if ((deleted % spam) == 0) {
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
		std::cout << "deleted " << deleted << "\n";
		if (util) {
			busyMeterThread->request_stop();
			busyMeterThread->join();
			delete (busyMeterThread);
		}

	} catch (exception& e) {
		std::cout << "something went wrong" << e.what();
	} catch (...) {
		std::cout << "something went wrong, and I have no idea what was!";
	}
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

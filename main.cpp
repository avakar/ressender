#include <libyb/net/net.hpp>
#include <libyb/net/http_server.hpp>
#include <libyb/async/group.hpp>
#include <libyb/async/sync_runner.hpp>

class app
{
public:
	yb::task<yb::http_response> operator()(yb::http_request const & req)
	{
		if (req.method == "GET")
		{
			yb::http_response resp;
			resp.status_code = 200;
			resp.headers.insert(std::make_pair("content-type", "text/event-stream; charset=utf-8"));
			resp.headers.insert(std::make_pair("content-length", "0"));
			return yb::async::value(resp);
		}

		yb::http_response resp;
		resp.status_code = 500;
		resp.headers.insert(std::make_pair("content-length", "0"));
		return yb::async::value(resp);
	}
};

int main()
{
	app a;
	yb::sync_runner runner;
	runner.run(yb::run_http_server(7589, std::ref(a)));
}

#include <libyb/net/http_server.hpp>
#include <libyb/async/await.hpp>
#include <set>
#include <deque>
#include <sstream>
#include <iostream>

#include <libyb/async/channel.hpp>
#include <libyb/async/timer.hpp>
#include <libyb/async/sigint.hpp>

struct mystream
	: public yb::istream
{
	typedef std::list<yb::channel<std::string>> list_t;

	mystream(list_t & list, list_t::iterator channel)
		: m_channels(list), m_channel(channel)
	{
	}

	~mystream()
	{
		m_channels.erase(m_channel);
		std::cout << "remove\n";
	}

	yb::task<yb::buffer_view> read_from_buf(yb::buffer_policy policy, size_t max_size)
	{
		return policy.fetch(1, m_buf.size()).then([this](yb::buffer buf) {
			size_t chunk = (std::min)(m_buf.size(), buf.size());
			std::copy(m_buf.begin(), m_buf.begin() + chunk, buf.data());
			m_buf.erase(m_buf.begin(), m_buf.begin() + chunk);
			return yb::buffer_view(std::move(buf), chunk);
		});
	}

	yb::task<yb::buffer_view> read(yb::buffer_policy policy, size_t max_size) override
	{
		if (m_buf.empty())
		{
			yb::channel<std::string> & chan = *m_channel;

			struct cont_t
			{
				cont_t(mystream * self, yb::buffer_policy && policy, size_t max_size)
					: self(self), policy(std::move(policy)), max_size(max_size)
				{
				}

				yb::task<yb::buffer_view> operator()(std::string s)
				{
					self->m_buf = std::move(s);
					return self->read_from_buf(std::move(policy), max_size);
				}

				mystream * self;
				yb::buffer_policy policy;
				size_t max_size;
			};

			return chan.receive().then(cont_t(this, std::move(policy), max_size));
		}
		else
		{
			return this->read_from_buf(std::move(policy), max_size);
		}
	}

	std::list<yb::channel<std::string>> & m_channels;
	std::list<yb::channel<std::string>>::iterator m_channel;
	std::string m_buf;
};

class app
{
public:
	app()
		: loop_index(0)
	{
	}

	yb::task<yb::http_response> operator()(yb::http_request const & req)
	{
		if (req.method == "GET")
		{
			yb::http_response resp;
			resp.status_code = 200;
			resp.headers.add("content-type", "text/event-stream");
			resp.headers.add("cache-control", "no-store");
			resp.headers.add("access-control-allow-origin", "*");

			m_channels.push_back(yb::channel<std::string>::create_infinite());
			std::cout << "add\n";
			resp.body = std::make_shared<mystream>(m_channels, std::prev(m_channels.end()));
			return yb::async::value(resp);
		}

		if (req.method == "OPTIONS")
		{
			yb::http_response resp;
			resp.status_code = 200;
			resp.headers.add("content-type", "text/event-stream");
			resp.headers.add("cache-control", "no-store");
			resp.headers.add("access-control-allow-origin", "*");
			resp.headers.add("content-length", "0");
			return yb::async::value(resp);
		}

		yb::http_response resp;
		resp.status_code = 500;
		resp.headers.add("content-length", "0");
		return yb::async::value(resp);
	}

	yb::task<void> run()
	{
		return yb::loop([this](yb::cancel_level cl) {
			return yb::wait_ms(1000).then([this]() {
				std::ostringstream ss;
				ss << "data: " << loop_index++ << "\r\n\r\n";

				for (auto && ch : m_channels)
					ch.send_sync(ss.str());
			});
		});
	}

private:
	std::list<yb::channel<std::string>> m_channels;
	size_t loop_index;
};

int main()
{
	app a;

	yb::sync_runner r;
	yb::task<void> server = r.post(yb::run_http_server(7589, std::ref(a)));
	yb::task<void> app = r.post(a.run());

	std::cout << "Press Ctrl+C to exit..." << std::endl;
	r.run(yb::wait_for_sigint());

	std::cout << "Exiting gracefully..." << std::endl;
	server.cancel(yb::cl_quit);
	r.run(std::move(server));
}

// Copyright 2021 Your Name <your_email>

#include <example.hpp>

auto start = std::chrono::high_resolution_clock::now();

struct Request {
  std::string input;
};

struct Field {
  size_t cost;
  std::string text;
};

std::vector<Field> unhandled;

struct Response {
  std::vector<std::pair<int, std::string>> suggestions;
};

void fail(beast::error_code ec, char const* what) {
  std::cerr << what << ": " << ec.message() << "\n";
}

void update_information_in_storage(std::vector<Field> updateData,
                                   std::string file) {
  std::fstream data;
  data.open(file);
  if (data.eof()) {
    data << "[" << std::endl;
    data << "]" << std::endl;
  }

  std::string fileData;
  std::ostringstream tmp;
  tmp << data.rdbuf();
  data.close();
  fileData = tmp.str();
  fileData = fileData.substr(0, fileData.size() - 3);
  std::stringstream filess;
  filess << fileData << "," << std::endl;
  std::cout << "edited" << fileData << std::endl;
  for (size_t i = 0; i < updateData.size(); i++) {
    json field = json{{"id", updateData[i].text},
                      {"name", updateData[i].text},
                      {"cost", updateData[i].cost}};
    filess << field;
    if (i != updateData.size() - 1) {
      filess << "," << std::endl;
    } else {
      filess << std::endl;
    }
  }
  filess << "]" << std::endl;
  data.open(file, std::ofstream::out | std::ofstream::trunc);
  data << filess.str();
  unhandled = std::vector<Field>{};
  return;
}

std::string search_in_storage(std::string input, json data) {
  Response resp;
  bool check;
  for (auto& el : data.items()) {
    json elem = el.value();
    if (elem.at("id") == input) {
      check = true;
      std::pair<int, std::string> field;
      field.first = elem.at("cost").get<int>();
      field.second = elem.at("name").get<std::string>();
      resp.suggestions.push_back(field);
    }
  }
  if (!check) {
    return "";
  }
  std::sort(resp.suggestions.begin(), resp.suggestions.end());
  std::stringstream ss;
  ss << "{" << std::endl;
  ss << "\"suggestions\": [" << std::endl;
  for (size_t i = 0; i < resp.suggestions.size(); i++) {
    json jField = json{{"text", resp.suggestions[i].second}, {"position", i}};
    ss << jField;
    if (i != resp.suggestions.size() - 1) {
      ss << "," << std::endl;
    } else {
      ss << std::endl;
    }
  }
  ss << "]" << std::endl;
  ss << "}" << std::endl;
  return ss.str();
}

template <class Body, class Allocator, class Send>
void handle_request(http::request<Body, http::basic_fields<Allocator>>&& req,
                    Send&& send) {
  auto const bad_request = [&req](beast::string_view why) {
    http::response<http::string_body> res{http::status::bad_request,
                                          req.version()};
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::content_type, "text/html");
    res.keep_alive(req.keep_alive());
    res.body() = std::string(why);
    res.prepare_payload();
    return res;
  };

  if (req.method() != http::verb::post)
    return send(bad_request("Поддерживается только метод POST"));
  std::cout << req.target() << std::endl;

  if (req.target().empty() || req.target()[0] != '/' ||
      req.target().find("v1/api/suggest") == beast::string_view::npos)
    return send(bad_request("Данный путь не обрабатывается"));

  http::response<http::string_body> res{http::status::ok, req.version()};
  res.set(http::field::content_type, "application/json");
  res.set(http::field::server, BOOST_BEAST_VERSION_STRING);

  if (req.body() == "") {
    return send(bad_request("Ошибка JSON запроса"));
  }

  json reqJson = json::parse(req.body());

  std::string input = reqJson.at("input");

  if (input == "") {
    return send(bad_request("Ошибка JSON запроса"));
  }
  std::stringstream ss;

  std::fstream file(PATH_TO_JSON);

  json data;
  file >> data;

  std::string body = search_in_storage(input, data);

  if (body == "") {
    bool exist = false;
    for (size_t i = 0; i < unhandled.size(); i++) {
      if (unhandled[i].text == input) {
        exist = true;
        unhandled[i].cost = unhandled[i].cost + 10;
        break;
      }
    }
    if (!exist) {
      Field newF;
      newF.cost = 10;
      newF.text = input;
      unhandled.push_back(newF);
    }
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> time = end - start;
    std::cout << time.count() << std::endl;
    if (time.count() > 10.0) {
      start = std::chrono::high_resolution_clock::now();
      update_information_in_storage(unhandled, PATH_TO_JSON);
    }
    res.body() =
        "{\n"
        "    \"suggestions\": [\n"
        "   "
        "    ]\n"
        "}";
    res.prepare_payload();
    return send(std::move(res));
  }
  res.body() = body;
  res.prepare_payload();
  return send(std::move(res));
}

template <class Stream>
struct send_lambda {
  Stream& stream_;
  bool& close_;
  beast::error_code& ec_;

  explicit send_lambda(Stream& stream, bool& close, beast::error_code& ec)
      : stream_(stream), close_(close), ec_(ec) {}

  template <bool isRequest, class Body, class Fields>
  void operator()(http::message<isRequest, Body, Fields>&& msg) const {
    close_ = msg.need_eof();

    http::serializer<isRequest, Body, Fields> sr{msg};
    http::write(stream_, sr, ec_);
  }
};

void do_session(tcp::socket& socket) {
  bool close = false;
  beast::error_code ec;

  beast::flat_buffer buffer;

  send_lambda<tcp::socket> lambda{socket, close, ec};

  for (;;) {
    http::request<http::string_body> req;
    http::read(socket, buffer, req, ec);
    if (ec == http::error::end_of_stream) break;
    if (ec) return fail(ec, "read");

    handle_request(std::move(req), lambda);
    if (ec) return fail(ec, "write");
    if (close) {
      break;
    }
  }
  socket.shutdown(tcp::socket::shutdown_send, ec);
}

auto example() -> void { throw std::runtime_error("not implemented"); }
void go_useful_server(int argc, char* argv[]) {
  try {
    if (argc != 4) {
      std::cerr << "Usage: http-server-sync <address> <port> <doc_root>\n"
                << "Example:\n"
                << "    http-server-sync 0.0.0.0 8080 .\n";
      return;
    }
    auto const address = net::ip::make_address(argv[1]);
    auto const doc_root = std::make_shared<std::string>(argv[3]);

    net::io_context ioc{1};

    tcp::acceptor acceptor{ioc, {address, 7777}};
    for (;;) {
      tcp::socket socket{ioc};
      acceptor.accept(socket);
      std::thread{std::bind(&do_session, std::move(socket))}.detach();
    }
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return;
  }
}

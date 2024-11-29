#include <cstdlib>
#include <iostream>
#include <fstream>
#include <sstream>
#include <memory>
#include <utility>
#include <deque>
#include <vector>
#include <string>
#include <boost/asio.hpp>

using boost::asio::ip::tcp;

class session : public std::enable_shared_from_this<session>
{
public:
  session(tcp::socket socket)
    : socket_(std::move(socket))
  {
  }

  void start()
  {
    read_message();
  }

private:
  void read_message()
  {
    auto self(shared_from_this());
    boost::asio::async_read_until(socket_, buffer_, "\r\n",
        [this, self](boost::system::error_code ec, std::size_t /*length*/)
        {
          if (!ec)
          {
            std::istream is(&buffer_);
            std::string message;
            std::getline(is, message, '\r'); // Remove o '\r' antes do '\n'
            is.ignore(1); // Ignora o '\n'

            std::cout << "Received: " << message << std::endl;
            
            process_message(message);

            read_message();
          }
        });
  }

  void process_message(const std::string& message)
  {
    std::istringstream iss(message);
    std::string command, sensor_id, num_records_str;

    std::getline(iss, command, '|');
    if (command == "LOG")
    {
      std::string data_hora, leitura;
      std::getline(iss, sensor_id, '|');
      std::getline(iss, data_hora, '|');
      std::getline(iss, leitura);
      write_log(sensor_id, data_hora, leitura);
    }
    else if (command == "GET")
    {
      std::getline(iss, sensor_id, '|');
      std::getline(iss, num_records_str);
      int num_records = std::stoi(num_records_str);
      send_log_data(sensor_id, num_records);
    }
    else
    {
      std::cerr << "Unknown command: " << command << std::endl;
    }
  }

  void write_log(const std::string& sensor_id, const std::string& data_hora, const std::string& leitura)
  {
    std::string filename = "sensor_" + sensor_id + ".log";

    // Open the file in binary append mode
    std::ofstream file(filename, std::ios::binary | std::ios::app);
    if (file)
    {
      uint16_t data_length = data_hora.size();
      uint16_t leitura_length = leitura.size();

      // Write lengths followed by the actual data
      file.write(reinterpret_cast<const char*>(&data_length), sizeof(data_length));
      file.write(data_hora.c_str(), data_length);
      file.write(reinterpret_cast<const char*>(&leitura_length), sizeof(leitura_length));
      file.write(leitura.c_str(), leitura_length);

      std::cout << "Log saved to " << filename << std::endl;
    }
    else
    {
      std::cerr << "Failed to open file: " << filename << std::endl;
    }
  }

  void send_log_data(const std::string& sensor_id, int num_records)
  {
    std::string filename = "sensor_" + sensor_id + ".log";
    std::ifstream file(filename, std::ios::binary);

    if (!file)
    {
      std::cerr << "Sensor log file not found: " << filename << std::endl;
      std::string error_message = "ERROR: Log file not found\r\n";
      boost::asio::async_write(socket_, boost::asio::buffer(error_message),
          [](boost::system::error_code /*ec*/, std::size_t /*length*/) {});
      return;
    }

    std::deque<std::string> records;
    while (file)
    {
      uint16_t data_length, leitura_length;
      file.read(reinterpret_cast<char*>(&data_length), sizeof(data_length));
      if (file.eof()) break;

      std::string data_hora(data_length, '\0');
      file.read(&data_hora[0], data_length);

      file.read(reinterpret_cast<char*>(&leitura_length), sizeof(leitura_length));
      std::string leitura(leitura_length, '\0');
      file.read(&leitura[0], leitura_length);

      records.push_back(data_hora + "|" + leitura);
      if (records.size() > num_records)
      {
        records.pop_front();
      }
    }

    std::ostringstream response;
    response << num_records << ";";
    for (const auto& record : records)
    {
      response << record << ";";
    }
    response.seekp(-1, std::ios_base::end); // Remove the last semicolon
    response << "\r\n";

    std::string response_str = response.str();
    boost::asio::async_write(socket_, boost::asio::buffer(response_str),
        [](boost::system::error_code /*ec*/, std::size_t /*length*/) {});
  }

  tcp::socket socket_;
  boost::asio::streambuf buffer_;
};

class server
{
public:
  server(boost::asio::io_context& io_context, short port)
    : acceptor_(io_context, tcp::endpoint(tcp::v4(), port))
  {
    accept();
  }

private:
  void accept()
  {
    acceptor_.async_accept(
        [this](boost::system::error_code ec, tcp::socket socket)
        {
          if (!ec)
          {
            std::make_shared<session>(std::move(socket))->start();
          }

          accept();
        });
  }

  tcp::acceptor acceptor_;
};

int main(int argc, char* argv[])
{
  if (argc != 2)
  {
    std::cerr << "Usage: chat_server <port>\n";
    return 1;
  }

  boost::asio::io_context io_context;

  server s(io_context, std::atoi(argv[1]));

  io_context.run();

  return 0;
}

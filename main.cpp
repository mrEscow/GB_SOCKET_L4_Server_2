#include <QCoreApplication>

/*
.Переработайте TCP-сервер так, чтобы внутри потоков обрабатывающих запросы клиента,
использовался Asio, либо Boost.Asio, т.е. работа с клиентскими запросами внутри потока
велась асинхронно. В итоге, каждый поток сервера мог обрабатывать несколько клиентов в
асинхронном режиме. Будет полезно совместить в одном приложении такой сервер и
возможность отдачи файла по частям из задания 1.

*/

#include <ctime>
#include <iostream>
#include <string>
#include <boost/system/error_code.hpp>
#include <boost/asio.hpp>


const int echo_port = 1989;
using boost::asio::ip::tcp;


std::string make_daytime_string(){
    using namespace std; // For time_t, time and ctime
    time_t now = time(0);
    return ctime(&now);
}


//-------------------------------------------------
void Synchronous_version();
void Asynchronous_version();

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);


    return a.exec();
}


void Synchronous_version(){
    try
    {
        boost::asio::io_context io_context;

        // Для прослушивания новых соединений будет создан объект
        // ip::tcp::acceptor на TCP-порту 1300 для IP версии 4
        tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), 1300));

        // Сервер принимает только одно соединение за раз
        for (;;){
            // Сформируется сокет, представляющий соединение с клиентом,
            // затем снова будет ожидаться соединение
            tcp::socket socket(io_context);
            acceptor.accept(socket);
            // Здесь клиент обратился к серверу. Передаём ему данные
            std::string message = make_daytime_string();
            // В чистом Asio это будет asio::error_code
            boost::system::error_code ignored_error;
            boost::asio::write(socket, boost::asio::buffer(message), ignored_error);
        }
    }
    catch (const std::exception& e)
    {
    std::cerr << e.what() << std::endl;
    }
}


// Указатель shared_ptr и enable_shared_from_this нужны для того,
// чтобы сохранить объект tcp_connection до завершения выполнения операции.
class TcpConnection : public std::enable_shared_from_this<TcpConnection>
{
public:
    typedef std::shared_ptr<TcpConnection> pointer;

    static pointer create(boost::asio::io_context& io_context){
        return pointer(new TcpConnection(io_context));
    }

    tcp::socket& socket(){
        return socket_;
    }

    // В методе start(), вызывается asio::async_write(), отправляющий данные
    //клиенту.
    // Здесь используется asio::async_write(), вместо
    // ip::tcp::socket::async_write_some(), чтобы весь блок данных был гарантированно
    // отправлен.
    void start(){
        // The data to be sent is stored in the class member message_ as we
        // need to keep the data valid until the asynchronous operation is complete.
        message_ = make_daytime_string();
        auto s = shared_from_this();
        // Здесь вместо boost::bind используется std::bind, чтобы уменьшить
        // число зависимостей от Boost.
        // Он не работает с плейсхолдерами из Boost.
        // В комментариях указаны альтернативные плейсхолдеры.
        boost::asio::async_write(socket_, boost::asio::buffer(message_),
        // handle_write() выполнит обработку запроса клиента.
        [s] (const boost::system::error_code& error, size_t bytes_transferred){
                s->handle_write(error, bytes_transferred);
            }
        );
    }
private:
    TcpConnection(boost::asio::io_context& io_context)
    : socket_(io_context)
    {
    }
    void handle_write(const boost::system::error_code& /*error*/, size_t bytes_transferred){
        std::cout << "Bytes transferred: " << bytes_transferred <<
        std::endl;
    }
private:
    tcp::socket socket_;
    std::string message_;
};

class TcpServer
{
public:
    // В конструкторе инициализируется акцептор, начинается прослушивание TCP
    // порта.
    TcpServer(boost::asio::io_context& io_context) :
    io_context_(io_context),
    acceptor_(io_context, tcp::endpoint(tcp::v4(), echo_port)){
        start_accept();
    }
private:
    // Метод start_accept() создаёт сокет и выполняет асинхронный `accept()`,
    // при соединении.
    void start_accept(){
        TcpConnection::pointer new_connection =
        TcpConnection::create(io_context_);
        acceptor_.async_accept(new_connection->socket(),
        [this, new_connection] (const boost::system::error_code& error){
                this->handle_accept(new_connection, error);
            }
        );
    }

    // Метод handle_accept() вызывается, когда асинхронный accept,
    // инициированный в start_accept() завершается.
    // Она выполняет обработку запроса клиента и запуск нового акцептора.
    void handle_accept(TcpConnection::pointer new_connection,
                       const boost::system::error_code& error){
        if (!error){
            new_connection->start();
        }
        start_accept();
    }
private:
    boost::asio::io_context& io_context_;
    tcp::acceptor acceptor_;
};


void Asynchronous_version(){
    try
    {
        // io_context предоставляет службы ввода-вывода, которые будет
        //использовать сервер, такие как сокеты.
        boost::asio::io_context io_context;
        TcpServer server(io_context);
        // Запуск асинхронных операций.
        io_context.run();
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << std::endl;
    }

}

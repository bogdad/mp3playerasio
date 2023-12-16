#pragma once

#include "asio-client.hpp"
#include "audio-player.hpp"
#include "protocol.hpp"
#include <asio/executor.hpp>
#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>
namespace am {

struct Song {
	std::string name;
};

class Driver {
public:
	Driver(asio::io_context &context, asio::io_context::strand &strand, std::string &&host);
	void play(Song &&song);

private:
	am::RingBuffer buffer_;
	asio::io_context &context_;
	asio::io_context::strand &strand_;
	asio::executor_work_guard<decltype(context_.get_executor())> work_guard_;
	std::string host_;

	Mp3Stream mp3_stream_;
	std::optional<AsioClient> asio_client_{};
};



} // namespace am

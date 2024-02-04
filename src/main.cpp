#include <cstdint>
#include <string>
#include <cstring>
#include <stdexcept>
#include <cstdio>
#include <cstdarg>
#include <vector>
#include <memory>
#include <array>
#include <system_error>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

struct end_of_stream : std::exception {
	const char * what() const noexcept { return "end of stream"; }
};

struct istream {
	inline virtual ~istream() noexcept = default;
	virtual size_t try_read(void * buffer, size_t amount) = 0;
	void read(void * buffer, size_t amount);
	template<typename T> void read(T & value);
	template<typename T> T read();
};

void istream::read(void * buffer, const size_t amount) {
	for (size_t remaining = amount; remaining > 0;) {
		const auto offset = amount - remaining;
		const auto dst = static_cast<char *>(buffer) + offset;
		const auto result = try_read(dst, remaining);
		if (result > 0) {
			remaining -= result;
		} else {
			throw end_of_stream{};
		}
	}
}

template<typename T>
inline void istream::read(T & value) {
	read(&value, sizeof(T));
}

template<typename T>
inline T istream::read() {
	T tmp;
	read(&tmp, sizeof(T));
	return tmp;
}

struct ostream {
	inline virtual ~ostream() noexcept = default;
	virtual size_t try_write(const void * buffer, size_t amount) = 0;
	void write(const void * buffer, size_t amount);
	template<typename T> void write(const T & value);
};

void ostream::write(const void * buffer, size_t amount) {
	for (size_t remaining = amount; remaining > 0;) {
		const auto offset = amount - remaining;
		const auto src = static_cast<const char *>(buffer) + offset;
		const auto result = try_write(src, remaining);
		if (result > 0) {
			remaining -= result;
		} else {
			throw end_of_stream{};
		}
	}
}

template<typename T>
inline void ostream::write(const T & value) {
	write(&value, sizeof(T));
}

struct iostream : istream, ostream {
	inline virtual ~iostream() noexcept = default;
};

struct ifstream : istream {
	inline virtual ~ifstream() noexcept = default;
	static std::unique_ptr<ifstream> open(const std::string & filename);
	virtual void seek(size_t offset) = 0;
};

struct ifstream_impl final : ifstream {
	int m_fd;

	inline ifstream_impl(const std::string & filename)
       	: m_fd(::open(filename.c_str(), O_RDONLY | O_CLOEXEC)) {
		if (m_fd < 0) {
			throw std::system_error(errno, std::system_category());
		}
	}

	~ifstream_impl() noexcept {
		::close(m_fd);
	}

	size_t try_read(void * buffer, const size_t amount) override {
		if (amount > 0) {
			const auto result = ::read(m_fd, buffer, amount);
			if (result >= 0) {
				return result;
			} else {
				throw std::system_error(errno, std::system_category());
			}
		} else {
			return 0;
		}
	}

	void seek(const size_t offset) override {
		if (::lseek(m_fd, offset, SEEK_SET) >= 0) {
			return;
		} else {
			throw std::system_error(errno, std::system_category());
		}
	}
};

std::unique_ptr<ifstream> ifstream::open(const std::string & filename) {
	return std::unique_ptr<ifstream>(new ifstream_impl(filename));
}

struct ofstream : iostream {
	inline virtual ~ofstream() noexcept = default;
	static std::unique_ptr<ofstream> create(const std::string & filename);
	virtual void seek(size_t offset) = 0;
};

struct ofstream_impl final : ofstream {
	int m_fd;

	inline ofstream_impl(const std::string & filename)
       	: m_fd(::open(filename.c_str(), O_RDWR | O_TRUNC | O_CREAT | O_CLOEXEC)) {
		if (m_fd < 0) {
			throw std::system_error(errno, std::system_category());
		}
	}

	~ofstream_impl() noexcept {
		::close(m_fd);
	}

	size_t try_read(void * buffer, const size_t amount) override {
		if (amount > 0) {
			const auto result = ::read(m_fd, buffer, amount);
			if (result >= 0) {
				return result;
			} else {
				throw std::system_error(errno, std::system_category());
			}
		} else {
			return 0;
		}
	}

	size_t try_write(const void * buffer, const size_t amount) override {
		if (amount > 0) {
			const auto result = ::write(m_fd, buffer, amount);
			if (result >= 0) {
				return result;
			} else {
				throw std::system_error(errno, std::system_category());
			}
		} else {
			return 0;
		}
	}

	void seek(const size_t offset) override {
		if (::lseek(m_fd, offset, SEEK_SET) >= 0) {
			return;
		} else {
			throw std::system_error(errno, std::system_category());
		}
	}
};

std::unique_ptr<ofstream> ofstream::create(const std::string & filename) {
	return std::unique_ptr<ofstream>(new ofstream_impl(filename));
}

struct ilzrwstream : istream {
	inline virtual ~ilzrwstream() noexcept = default;
	static std::unique_ptr<ilzrwstream> wrap(istream &);
};

struct ilzrwstream_impl : ilzrwstream {
	istream & m_stream;
	uint8_t m_window[4096];
	uint16_t m_window_index;
	uint16_t m_control_bits;
	uint8_t m_control_remaining;

	inline ilzrwstream_impl(istream & stream) noexcept
	: m_stream(stream)
	, m_window_index(0)
	, m_control_bits(0)
	, m_control_remaining(0)
	{}

	size_t try_read(void * buffer, size_t amount) override {
		for (size_t remaining = amount; remaining > 0;) {
			const auto offset = amount - remaining;
			const auto dst = static_cast<uint8_t *>(buffer) + offset;
			if (m_control_remaining == 0) {
				m_stream.read(m_control_bits);
				m_control_remaining = 16;
			}
			if (m_control_bits & 0x1) {
				const auto tmp = m_stream.read<uint16_t>();
				const size_t ofs = ((tmp & 0xf000) >> 4) | (tmp & 0xff);
				const size_t len = ((tmp & 0xf00) >> 8) + 1;
				for (size_t i = 0; i < len; ++i) {
					const auto index = ((m_window_index + (sizeof(m_window) - ofs)) % sizeof(m_window)) + i;
					*dst = m_window[index];
				}
			} else {
				const auto value = m_stream.read<uint8_t>();
				*dst = value;
				m_window[m_window_index] = value;
				m_window_index = (m_window_index + 1) % sizeof(m_window);
			}
		}
		return 0;
	}
};

std::unique_ptr<ilzrwstream> ilzrwstream::wrap(istream & stream) {
	return std::unique_ptr<ilzrwstream>(new ilzrwstream_impl(stream));
}

std::string derive_output_filename(
	const std::string & input_filename,
	size_t index
) {
	const auto dot = input_filename.find_last_of('.');
	if (dot != std::string::npos) {
		const auto format = "%.*s.%zu.bmp";
		const auto len = snprintf(nullptr, 0, format, int(dot), input_filename.c_str(), index);
		std::string output_filename(len, 0);
		snprintf(&output_filename[0], len + 1, format, int(dot), input_filename.c_str(), index);
		return output_filename;
	} else {
		return std::string{};
	}
}

auto error(const char * format, ...) {
	va_list args;

	va_start(args, format);
	const auto len = vsnprintf(nullptr, 0, format, args);
	va_end(args);

	std::vector<char> buffer(len + 1);
	va_start(args, format);
	vsnprintf(buffer.data(), buffer.size(), format, args);
	va_end(args);

	return std::runtime_error(buffer.data());
}

void export_bitmap(
	istream & input,
	const std::string & filename,
	const size_t amount
) {
	auto output = ofstream::create(filename);
	char buffer[4096];
	for (size_t remaining = amount; remaining > 0;) {
		const auto len = std::min(remaining, sizeof(buffer));
		input.read(buffer, len);
		output->write(buffer, len);
		remaining -= len;
	}
}

void export_plain_bitmap(
	istream & stream,
	const std::string & input_filename,
	const size_t index
) {
	const auto size = stream.read<uint32_t>();
	const auto output_filename = derive_output_filename(input_filename, index);
	export_bitmap(stream, output_filename, size);
}

void export_compressed_bitmap(
	istream & stream,
	const std::string & input_filename,
	const size_t index
) {
	const auto size1 = stream.read<uint32_t>();
	const auto size2 = stream.read<uint32_t>();
	const auto unknown = stream.read<uint8_t>();
	const auto output_filename = derive_output_filename(input_filename, index);
	const auto compressed = ilzrwstream::wrap(stream);
	export_bitmap(*compressed, output_filename, size1);
}

bool is_bitmap_list(const std::array<char, 8> & magic) noexcept {
	return memcmp(&magic, "LMDBML30", sizeof(magic)) == 0;
}

void extract(const std::string & input_filename) {
	puts(input_filename.c_str());
	auto stream = ifstream::open(input_filename);
	const auto version = stream->read<uint8_t>();
	if (version == 8) {
		const auto magic = stream->read<std::array<char,8>>();
		if (is_bitmap_list(magic)) {
			const auto count = stream->read<uint32_t>();
			std::vector<uint32_t> offsets(count);
			for (size_t i = 0; i < count; ++i) {
				offsets[i] = stream->read<uint32_t>();
			}

			for (size_t i = 0; i < count; ++i) {
				stream->seek(offsets[i]);
				const auto type = stream->read<uint8_t>();
				if (type == 8) {
					export_plain_bitmap(*stream, input_filename, i);
				} else if (type == 9) {
					export_compressed_bitmap(*stream, input_filename, i);
				} else {
					throw error("unknown type: %u", type);
				}
			}
		} else {
			puts("Does not contain images");
		}
	} else {
		throw error("unknown version: %u", version);
	}
}

int main(int argc, const char ** argv) {
	if (argc > 1) {
		extract(argv[1]);
	}
	return 0;
}

#pragma once

#include "lib/alloc.h"
#include "lib/mem.h"
#include "lib/str.h"

#include <stddef.h>
#include <stdint.h>

template <typename T> class Vec {
  public:
	Vec() : data_(nullptr), len_(0), cap_(0) {}
	Vec(const Vec& o) : data_(nullptr), len_(0), cap_(0) { copy_from(o); }
	Vec(Vec&& o) noexcept : data_(o.data_), len_(o.len_), cap_(o.cap_) {
		o.data_ = nullptr;
		o.len_ = 0;
		o.cap_ = 0;
	}
	Vec& operator=(const Vec& o) {
		if (this != &o)
			copy_from(o);
		return *this;
	}
	Vec& operator=(Vec&& o) noexcept {
		if (this != &o) {
			reset();
			data_ = o.data_;
			len_ = o.len_;
			cap_ = o.cap_;
			o.data_ = nullptr;
			o.len_ = 0;
			o.cap_ = 0;
		}
		return *this;
	}
	~Vec() { reset(); }

	void push(const T& v) {
		grow_if_needed(1);
		data_[len_++] = v;
	}
	void pop() {
		if (len_)
			--len_;
	}
	void clear() { len_ = 0; }
	void reserve(size_t n) {
		if (n > cap_)
			grow_to(n);
	}
	void truncate(size_t n) { len_ = n < len_ ? n : len_; }

	T& operator[](size_t i) { return data_[i]; }
	const T& operator[](size_t i) const { return data_[i]; }
	T& at(size_t i) { return data_[i < len_ ? i : 0]; }

	size_t size() const { return len_; }
	size_t capacity() const { return cap_; }
	bool empty() const { return len_ == 0; }
	T* data() { return data_; }
	const T* data() const { return data_; }

  private:
	void reset() {
		free(data_);
		data_ = nullptr;
		len_ = 0;
		cap_ = 0;
	}
	void grow_if_needed(size_t more) {
		if (len_ + more > cap_)
			grow_to(len_ + more);
	}
	void grow_to(size_t want) {
		size_t n = cap_ ? cap_ * 2 : 4;
		if (n < want)
			n = want;
		T* fresh = (T*)malloc(n * sizeof(T));
		if (!fresh)
			return;
		for (size_t i = 0; i < len_; ++i)
			fresh[i] = data_[i];
		free(data_);
		data_ = fresh;
		cap_ = n;
	}
	void copy_from(const Vec& o) {
		if (!o.len_)
			return;
		T* fresh = (T*)malloc(o.len_ * sizeof(T));
		if (!fresh)
			return;
		for (size_t i = 0; i < o.len_; ++i)
			fresh[i] = o.data_[i];
		data_ = fresh;
		len_ = o.len_;
		cap_ = o.len_;
	}

	T* data_;
	size_t len_;
	size_t cap_;
};

class String {
  public:
	String() : buf_(nullptr), len_(0), cap_(0) {}
	String(const char* s) : buf_(nullptr), len_(0), cap_(0) { assign(s); }
	String(const String& o) : buf_(nullptr), len_(0), cap_(0) { assign(o.c_str()); }
	String& operator=(const String& o) {
		if (this != &o)
			assign(o.c_str());
		return *this;
	}
	~String() { free(buf_); }

	void assign(const char* s) {
		clear();
		if (!s)
			return;
		const size_t n = strlen(s);
		reserve(n + 1);
		if (!buf_)
			return;
		for (size_t i = 0; i < n; ++i)
			buf_[i] = s[i];
		buf_[n] = 0;
		len_ = n;
	}
	void push_back(char c) {
		reserve(len_ + 2);
		if (!buf_)
			return;
		buf_[len_++] = c;
		buf_[len_] = 0;
	}
	void clear() {
		len_ = 0;
		if (buf_)
			buf_[0] = 0;
	}

	const char* c_str() const { return buf_ ? buf_ : ""; }
	size_t size() const { return len_; }
	size_t length() const { return len_; }
	bool empty() const { return len_ == 0; }
	bool operator==(const String& o) const { return strcmp(c_str(), o.c_str()) == 0; }

  private:
	void reserve(size_t want) {
		if (want <= cap_)
			return;
		size_t n = cap_ ? cap_ * 2 : 16;
		if (n < want)
			n = want;
		char* fresh = (char*)malloc(n);
		if (!fresh)
			return;
		if (buf_) {
			for (size_t i = 0; i < len_; ++i)
				fresh[i] = buf_[i];
		}
		fresh[len_] = 0;
		free(buf_);
		buf_ = fresh;
		cap_ = n;
	}

	char* buf_;
	size_t len_;
	size_t cap_;
};

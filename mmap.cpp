#include <node.h>
#include <node_buffer.h>

#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>

struct hint_wrap {
	size_t length;
};

// The two methods below were copied from
// https://github.com/electron/electron?branch=master&filepath=atom/common/api/atom_api_v8_util.cc
// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license.

v8::Local<v8::Value> GetHiddenValue(v8::Isolate* isolate,
			      v8::Local<v8::Object> object,
			      v8::Local<v8::String> key) {
	v8::Local<v8::Context> context = isolate->GetCurrentContext();
	v8::Local<v8::Private> privateKey = v8::Private::ForApi(isolate, key);
	v8::Local<v8::Value> value;
	v8::Maybe<bool> result = object->HasPrivate(context, privateKey);
	if (!(result.IsJust() && result.FromJust()))
		return v8::Local<v8::Value>();
	if (object->GetPrivate(context, privateKey).ToLocal(&value))
		return value;
	return v8::Local<v8::Value>();
}

void SetHiddenValue(v8::Isolate* isolate,
	      v8::Local<v8::Object> object,
	      v8::Local<v8::String> key,
	      v8::Local<v8::Value> value) {
	if (value.IsEmpty())
		return;
	v8::Local<v8::Context> context = isolate->GetCurrentContext();
	v8::Local<v8::Private> privateKey = v8::Private::ForApi(isolate, key);
	object->SetPrivate(context, privateKey, value);
}

static void Map_finalise(char *data, void*hint_void)
{
	struct hint_wrap *h = (struct hint_wrap *)hint_void;

	if(h->length > 0) {
		munmap(data, h->length);
	}
	delete h;
}

void Sync(const v8::FunctionCallbackInfo<v8::Value>& args)
{
	auto *isolate = args.GetIsolate();
	auto context = isolate->GetCurrentContext();
	auto buffer = args.This()->ToObject(context).ToLocalChecked();
	char *data = node::Buffer::Data(buffer);
	size_t length = node::Buffer::Length(buffer);

	// First optional argument: offset
	if (args.Length() > 0) {
		const size_t offset = args[0]->ToInteger(isolate)->Value();
		if(length <= offset) return;

		data += offset;
		length -= offset;
	}

	// Second optional argument: length
	if (args.Length() > 1) {
		const size_t range = args[1]->ToInteger(isolate)->Value();
		if(range < length) length = range;
	}

	// Third optional argument: flags
	int flags;
	if (args.Length() > 2) {
		flags = args[2]->ToInteger(isolate)->Value();
	} else {
		flags = MS_SYNC;
	}

	args.GetReturnValue().Set((0 == msync(data, length, flags)) ? v8::True(isolate) : v8::False(isolate));
}

void Unmap(const v8::FunctionCallbackInfo<v8::Value>& args)
{
	auto *isolate = args.GetIsolate();
	v8::Local<v8::Context> context = isolate->GetCurrentContext();
	auto buffer = args.This()->ToObject(context).ToLocalChecked();
	char *data = node::Buffer::Data(buffer);

	struct hint_wrap *d = (struct hint_wrap *)v8::External::Cast(*GetHiddenValue(isolate, buffer, v8::String::NewFromUtf8(isolate,"mmap_dptr")))->Value();

	bool ok = true;

	if(d->length > 0 && -1 == munmap(data, d->length)) {
		ok = false;
	} else {
		d->length = 0;
		(void)buffer->CreateDataProperty(isolate->GetCurrentContext(),
			v8::String::NewFromUtf8(isolate, "length"),
			v8::Number::New(isolate, 0));
	}

	args.GetReturnValue().Set(ok? v8::True(isolate): v8::False(isolate));
}

void Map(const v8::FunctionCallbackInfo<v8::Value>& args)
{
	auto *isolate = args.GetIsolate();
	v8::Local<v8::Context> context = isolate->GetCurrentContext();

	if (args.Length() <= 3)
	{
		isolate->ThrowException(
			v8::Exception::Error(
				v8::String::NewFromUtf8(isolate, "mmap() takes 4 arguments: size, protection, flags, fd and offset.")));
		return;
	}

	const size_t length  = args[0]->ToInteger(isolate)->Value();
	const int protection = args[1]->ToInteger(isolate)->Value();
	const int flags      = args[2]->ToInteger(isolate)->Value();
	const int fd         = args[3]->ToInteger(isolate)->Value();
	const off_t offset   = args[4]->ToInteger(isolate)->Value();

	char* data = (char *) mmap(0, length, protection, flags, fd, offset);

	if(data == MAP_FAILED)
	{
		isolate->ThrowException(node::ErrnoException(isolate, errno, "mmap", ""));
		return;
	}

	struct hint_wrap *d = new hint_wrap;
	d->length = length;

	auto buffer = node::Buffer::New(isolate, data, length, Map_finalise, (void*)d).ToLocalChecked();
	auto buffer_object = buffer->ToObject(context).ToLocalChecked();

	buffer_object->Set(v8::String::NewFromUtf8(isolate, "unmap"), v8::FunctionTemplate::New(isolate, Unmap)->GetFunction(context).ToLocalChecked());
	buffer_object->Set(v8::String::NewFromUtf8(isolate, "sync"), v8::FunctionTemplate::New(isolate, Sync)->GetFunction(context).ToLocalChecked());
	SetHiddenValue(isolate, buffer_object, v8::String::NewFromUtf8(isolate,"mmap_dptr"), v8::External::New(isolate, (void*)d));

	args.GetReturnValue().Set(buffer);
}


static void RegisterModule(v8::Local<v8::Object> exports)
{
	const int PAGESIZE = sysconf(_SC_PAGESIZE);

	NODE_SET_METHOD(exports, "map", Map);
	NODE_DEFINE_CONSTANT(exports, PROT_READ);
	NODE_DEFINE_CONSTANT(exports, PROT_WRITE);
	NODE_DEFINE_CONSTANT(exports, PROT_EXEC);
	NODE_DEFINE_CONSTANT(exports, PROT_NONE);
	NODE_DEFINE_CONSTANT(exports, MAP_SHARED);
	NODE_DEFINE_CONSTANT(exports, MAP_PRIVATE);
	NODE_DEFINE_CONSTANT(exports, PAGESIZE);
	NODE_DEFINE_CONSTANT(exports, MS_ASYNC);
	NODE_DEFINE_CONSTANT(exports, MS_SYNC);
	NODE_DEFINE_CONSTANT(exports, MS_INVALIDATE);
}

NODE_MODULE(mmap, RegisterModule);

#ifndef HTTPSERVER_00_HTTPSERVER_H
#define HTTPSERVER_00_HTTPSERVER_H

void invoke_cpp_callback(SEXP data, SEXP callback_xptr);

std::string do_encode_uri(std::string value, bool encode_reserved);
std::string do_decode_uri(std::string value, bool component);

#endif

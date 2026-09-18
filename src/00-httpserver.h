#ifndef HTTPSERVER_00_HTTPSERVER_H
#define HTTPSERVER_00_HTTPSERVER_H

void invokeCppCallback(SEXP data, SEXP callback_xptr);

std::string doEncodeURI(std::string value, bool encodeReserved);
std::string doDecodeURI(std::string value, bool component);

#endif

#ifndef _H_PULLOTA
#define _H_PULLOTA

#include <functional>
#include "yuarel.h"
#include <WiFiClientSecure.h>

#define PULLOTA_SUCCESS           0
#define PULLOTA_NO_NEW_UPDATE     1
#define PULLOTA_ERROR_URLPARSE    0x10
#define PULLOTA_ERROR_CONNECT     0x20
#define PULLOTA_ERROR_HTTP_STATUS 0x21
#define PULLOTA_ERROR_NO_CONTENT_LENGTH 0x22
#define PULLOTA_ERROR_INVALID_CONTENT_TYPE 0x23
#define PULLOTA_ERROR_NO_LAST_MODIFIED 0x24
#define PULLOTA_ERROR_NOT_FOUND 0x25
#define PULLOTA_ERROR_HTTP_AUTH 0x26
#define PULLOTA_ERROR_STARTING_UPDATE 0x30
#define PULLOTA_ERROR_SHORT_WRITE 0x31
#define PULLOTA_ERROR_UPDATE_INCOMPLETE 0x32
#define PULLOTA_ERROR_PERSISTING_LAST_MODIFIED 0x40

class PullOTA 
{
public:
  char *bearerToken;
  PullOTA(WiFiClient *_wifiClient, WiFiClientSecure *_wiFiClientSecure) 
  : wifiClient(_wifiClient), wiFiClientSecure(_wiFiClientSecure), bearerToken(NULL) {}

  ~PullOTA() {}
  
  int pull(const char* _url, std::function<void(int, int)> _onProgress);

private:
  WiFiClient *wifiClient;
  WiFiClientSecure *wiFiClientSecure;

  void buildRequest(struct yuarel *url, char *request, size_t size);
  void buildAuthorizationHeader(struct yuarel *url, char *authorizationHeader, size_t size);
  void buildIfModifiedSinceHeader(char *ifModifiedSinceHeader, size_t size);

  int readHeaders(WiFiClient *client, int *contentLength, String *lastModified);
  String getHeaderValue(String header, String headerName);

  int flashUpdate(WiFiClient *client, int contentLength, std::function<void(int, int)> _onProgress);

  int persistLastModifiedSince(String *lastModifiedSince);
};

#endif

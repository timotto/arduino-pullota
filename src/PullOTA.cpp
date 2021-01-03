#include <Arduino.h>
#include "PullOTA.h"
#include <base64.h>
#include <FS.h>
#include <SPIFFS.h>
#include <Update.h>

int PullOTA::pull(const char *_url, std::function<void(int, int)> _onProgress)
{
  char urlString[512];
  strncpy(urlString, _url, sizeof(urlString));

  struct yuarel url;
  if (yuarel_parse(&url, urlString) == -1)
  {
    return PULLOTA_ERROR_URLPARSE;
  }

  char request[2048];
  buildRequest(&url, request, sizeof(request));

  const bool https = strcmp(url.scheme, "https") == 0;
  const int port = (url.port != 0) ? url.port : (https ? 443 : 80);

  WiFiClient *client = https ? wiFiClientSecure : wifiClient;
  
  if (!client->connect(url.host, port))
  {
    return PULLOTA_ERROR_CONNECT;
  }

  client->print(request);

  String lastModified = "";
  int contentLength = -1;
  int err;

  err = readHeaders(client, &contentLength, &lastModified);
  if (err != PULLOTA_SUCCESS)
  {
    return err;
  }

  err = flashUpdate(client, contentLength, _onProgress);
  
  if (err != PULLOTA_SUCCESS)
  {
    return err;
  }

  err = persistLastModifiedSince(&lastModified);
  if (err != PULLOTA_SUCCESS)
  {
    return err;
  }

  return PULLOTA_SUCCESS;
}

void PullOTA::buildRequest(struct yuarel *url, char *request, size_t size)
{

  char authorizationHeader[1024];
  buildAuthorizationHeader(url, authorizationHeader, sizeof(authorizationHeader));

  char ifModifiedSinceHeader[256];
  buildIfModifiedSinceHeader(ifModifiedSinceHeader, sizeof(ifModifiedSinceHeader));

  snprintf(request, size,
           "GET /%s%s%s HTTP/1.0\r\n"
           "Host: %s\r\n"
           "User-Agent: NerfBL\r\n"
           "%s"
           "%s"
           "Connection: close\r\n"
           "\r\n",
           url->path == NULL ? "" : url->path,
           url->query == NULL ? "" : "?",
           url->query == NULL ? "" : url->query,
           url->host, authorizationHeader, ifModifiedSinceHeader);
}

void PullOTA::buildAuthorizationHeader(struct yuarel *url, char *authorizationHeader, size_t size)
{
  snprintf(authorizationHeader, size, "");

  if (bearerToken != NULL)
  {
    snprintf(authorizationHeader, size, "Authorization: Bearer %s\r\n", bearerToken);
    return;
  }

  if (url->username == NULL || url->password == NULL)
  {
    return;
  }

  char buffer[128];
  snprintf(buffer, sizeof(buffer), "%s:%s", url->username, url->password);
  String basicAuthString = base64::encode((uint8_t *)buffer, strlen(buffer));
  snprintf(authorizationHeader, size, "Authorization: Basic %s\r\n", basicAuthString.c_str());
}

void PullOTA::buildIfModifiedSinceHeader(char *ifModifiedSinceHeader, size_t size)
{
  sprintf(ifModifiedSinceHeader, "");
  File firmwareDateFile = SPIFFS.open("/firmware.date", FILE_READ);
  if (!firmwareDateFile)
  {
    return;
  }

  char buffer[128];
  const int len = firmwareDateFile.read((uint8_t *)buffer, 128 - 1);
  firmwareDateFile.close();
  if (len == 0)
  {
    return;
  }

  buffer[len] = 0;
  snprintf(ifModifiedSinceHeader, size, "If-Modified-Since: %s\r\n", buffer);
}

int PullOTA::readHeaders(WiFiClient *client, int *contentLength, String *lastModified)
{
  bool isValidContentType;

  while (client->connected())
  {
    String line = client->readStringUntil('\n');
    if (line == "\r")
    {
      break;
    }
    if (line.endsWith("\r")) {
      line = line.substring(0, line.length() - 1);
    }
    String lowerCaseLine = line;
    lowerCaseLine.toLowerCase();

    if (lowerCaseLine.startsWith("http/1."))
    {
      if (lowerCaseLine.indexOf("304") > 0)
      {
        return PULLOTA_NO_NEW_UPDATE;
      }

      if (lowerCaseLine.indexOf("404") > 0)
      {
        return PULLOTA_ERROR_NOT_FOUND;
      }

      if ((lowerCaseLine.indexOf("401") > 0) || (lowerCaseLine.indexOf("403") > 0))
      {
        return PULLOTA_ERROR_HTTP_AUTH;
      }

      if (lowerCaseLine.indexOf("200") < 0)
      {
        return PULLOTA_ERROR_HTTP_STATUS;
      }
    }

    if (lowerCaseLine.startsWith("content-length: "))
    {
      *contentLength = atoi((getHeaderValue(line, "content-length: ")).c_str());
    }

    if (lowerCaseLine.startsWith("content-type: "))
    {
      isValidContentType = getHeaderValue(line, "content-type: ") == "application/octet-stream" || getHeaderValue(line, "content-type: ") == "binary/octet-stream";
    }

    if (lowerCaseLine.startsWith("last-modified: "))
    {
      *lastModified = getHeaderValue(line, "last-modified: ");
    }
  }

  if (*contentLength == -1)
  {
    return PULLOTA_ERROR_NO_CONTENT_LENGTH;
  }

  if (!isValidContentType)
  {
    return PULLOTA_ERROR_INVALID_CONTENT_TYPE;
  }

  if (*lastModified == "")
  {
    return PULLOTA_ERROR_NO_LAST_MODIFIED;
  }

  return PULLOTA_SUCCESS;
}

String PullOTA::getHeaderValue(String header, String headerName)
{
  return header.substring(strlen(headerName.c_str()));
}

int PullOTA::flashUpdate(WiFiClient *client, int contentLength, std::function<void(int, int)> _onProgress)
{
  if (!Update.begin(contentLength))
  {
    return PULLOTA_ERROR_STARTING_UPDATE;
  }

  uint8_t buffer[1024];
  int bytesRead, bytesWritten, bytesWrittenTotal = 0;
  while (client->connected())
  {
    bytesRead = client->read(buffer, sizeof(buffer));
    if (bytesRead <= 0)
    {
      continue;
    }

    bytesWritten = Update.write(buffer, bytesRead);
    bytesWrittenTotal += bytesWritten;
    _onProgress(bytesWrittenTotal, contentLength);

    if (bytesRead != bytesWritten)
    {
      return PULLOTA_ERROR_SHORT_WRITE;
    }

    if (bytesWrittenTotal == contentLength)
    {
      Update.end();
      break;
    }
  }

  if (!Update.isFinished())
  {
    return PULLOTA_ERROR_UPDATE_INCOMPLETE;
  }

  return PULLOTA_SUCCESS;
}

int PullOTA::persistLastModifiedSince(String *lastModifiedSince)
{
  File otaDateFile = SPIFFS.open("/firmware.date", FILE_WRITE);
  if (!otaDateFile)
  {
    return PULLOTA_ERROR_PERSISTING_LAST_MODIFIED;
  }

  otaDateFile.write((uint8_t*)(lastModifiedSince->c_str()), lastModifiedSince->length()+1);
  otaDateFile.flush();
  otaDateFile.close();

  return PULLOTA_SUCCESS;
}

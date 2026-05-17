#pragma once
#include <Arduino.h>
#include <vector>

namespace proxy_api {

struct Profile {
  String id;
  String label;
  String provider;
  String model;
};

struct FetchResult {
  bool   ok;
  char   error[80];
  std::vector<Profile> profiles;
};

// Runtime endpoint configuration. Call once before fetch_profiles().
void configure(const String& host, uint16_t port, const String& bearer);

// GET /v1/profiles via the WG tunnel; return the parsed list.
FetchResult fetch_profiles();

}  // namespace proxy_api

local({
  # encode_uri and encode_uricomponent ----

  # "abc \ue5 \u4e2d" is identical to "abc å 中" when the system's encoding is
  # utf-8. however, the former is always encoded as utf-8, while the latter will
  # be encoded using the system's native encoding.
  utf8_str <- "abc \ue5 \u4e2d\r\n"
  utf8_str_encoded <- "abc%20%C3%A5%20%E4%B8%AD%0D%0A"
  reserved_str <- ",/?:@"
  reserved_str_encoded <- "%2C%2F%3F%3A%40"

  expect_true(Encoding(utf8_str) == "UTF-8")

  expect_identical(encode_uri(utf8_str), utf8_str_encoded)
  expect_identical(encode_uri_component(utf8_str), utf8_str_encoded)
  expect_identical(decode_uri(utf8_str_encoded), utf8_str)
  expect_identical(decode_uricomponent(utf8_str_encoded), utf8_str)
  expect_true(Encoding(decode_uri(utf8_str_encoded)) == "UTF-8")
  expect_true(Encoding(decode_uricomponent(utf8_str_encoded)) == "UTF-8")

  # behavior with reserved characters differs between encode_uri and
  # encode_uricomponent.
  expect_identical(encode_uri(reserved_str), reserved_str)
  expect_identical(encode_uri_component(reserved_str), reserved_str_encoded)
  expect_identical(decode_uri(reserved_str_encoded), reserved_str_encoded)
  expect_identical(decode_uricomponent(reserved_str_encoded), reserved_str)

  # decoding characters that aren't encoded should have no effect.
  expect_identical(decode_uri(utf8_str), utf8_str)
  expect_identical(decode_uricomponent(utf8_str), utf8_str)
  expect_true(Encoding(decode_uri(utf8_str)) == "UTF-8")
  expect_true(Encoding(decode_uricomponent(utf8_str)) == "UTF-8")
  expect_identical(decode_uri(reserved_str), reserved_str)
  expect_identical(decode_uricomponent(reserved_str), reserved_str)

  # vector input
  expect_identical(
    encode_uri(c(reserved_str, utf8_str)),
    c(reserved_str, utf8_str_encoded)
  )
  expect_identical(
    encode_uri_component(c(reserved_str, utf8_str)),
    c(reserved_str_encoded, utf8_str_encoded)
  )
  expect_identical(
    decode_uri(c(reserved_str_encoded, utf8_str_encoded)),
    c(reserved_str_encoded, utf8_str)
  )
  expect_identical(
    decode_uricomponent(c(reserved_str_encoded, utf8_str_encoded)),
    c(reserved_str, utf8_str)
  )

  # na handling
  expect_identical(encode_uri(NA_character_), NA_character_)
  expect_identical(encode_uri_component(NA_character_), NA_character_)
  expect_identical(decode_uri(NA_character_), NA_character_)
  expect_identical(decode_uricomponent(NA_character_), NA_character_)

  # strings that are not utf-8 encoded should be automatically converted to
  # utf-8 before url-encoding.
  #
  # "å", in utf-8. the previous string, with chinese characters, can't be
  # converted to latin1.
  utf8_str <- "\ue5"
  latin1_str <- iconv(utf8_str, "utf-8", "latin1")

  expect_identical(encode_uri(utf8_str), "%C3%A5")
  expect_identical(encode_uri(latin1_str), "%C3%A5")
  expect_identical(encode_uri_component(utf8_str), "%C3%A5")
  expect_identical(encode_uri_component(latin1_str), "%C3%A5")
})

local({
  # ip_family works correctly ----
  
  expect_identical(ip_family("127.0.0.1"), 4L)
  expect_identical(ip_family("0.0.0.0"), 4L)
  expect_identical(ip_family("192.168.0.1"), 4L)

  expect_identical(ip_family("::1"), 6L)
  expect_identical(ip_family("::"), 6L)
  expect_identical(ip_family("fe80::91:5800:400a:075c"), 6L)
  expect_identical(ip_family("fe80::1"), 6L)

  # ipv6 with zone id
  expect_identical(ip_family("::1%lo0"), 6L)
  expect_identical(ip_family("::%1"), 6L)
  expect_identical(ip_family("fe80::91:5800:400a:075c%en0"), 6L)
  expect_identical(ip_family("fe80::1%abcd"), 6L)

  expect_identical(ip_family("fe80::91:5800:400a:%075c"), -1L)
  expect_identical(ip_family(":::1"), -1L)
  expect_identical(ip_family(":1"), -1L)
  expect_identical(ip_family("127.0.0.1%1"), -1L)
  expect_identical(ip_family("10.0.0.500"), -1L)
  expect_identical(ip_family("0.0.200"), -1L)
  expect_identical(ip_family("123"), -1L)
  expect_identical(ip_family("localhost"), -1L)
})

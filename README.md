# AmLang Web Client Library

A comprehensive HTTP/HTTPS client library for AmLang applications, providing easy-to-use web request functionality with SSL/TLS support.

## Features

- **HTTP/HTTPS Support**: Full support for both HTTP and HTTPS protocols with SSL/TLS encryption
- **Multiple HTTP Methods**: GET, POST, PUT, DELETE operations
- **Structured Responses**: Rich HttpResponse objects with status codes, headers, and body access
- **URL Parsing**: Robust URL parsing with protocol detection and port handling
- **Cross-Platform**: Support for AmigaOS, macOS, Linux, and other platforms
- **SSL Integration**: Seamless SSL/TLS support via am-ssl library
- **Memory Efficient**: Stream-based response handling for large payloads

## Quick Start

### Basic GET Request

```amlang
use Am.Web.Client

var client = WebClient()
var response = client.get("http://httpbin.org/get")

("Status: " + response.getStatusCode().toString()).println()
("Body: " + response.getBodyAsString()).println()
```

### POST Request with Form Data

```amlang
use Am.Web.Client

var client = WebClient()
var response = client.post("http://httpbin.org/post", "name=AmLang&version=1.0")

("Status: " + response.getStatusCode().toString()).println()
("Response: " + response.getBodyAsString()).println()
```

### HTTPS Request

```amlang
use Am.Web.Client

var client = WebClient()
var response = client.get("https://httpbin.org/get")

if response.isSuccess() {
    ("Secure response received: " + response.getBodyAsString()).println()
} else {
    ("Error: " + response.getStatusCode().toString()).println()
}
```

## API Reference

### WebClient

The main class for making HTTP requests.

#### Methods

- `get(url: String): HttpResponse` - Make a GET request
- `post(url: String, data: String): HttpResponse` - Make a POST request with data
- `put(url: String, data: String): HttpResponse` - Make a PUT request with data
- `delete(url: String): HttpResponse` - Make a DELETE request

### HttpResponse

Represents an HTTP response with full access to status, headers, and body.

#### Methods

- `getStatusCode(): Int` - Get HTTP status code (200, 404, etc.)
- `getStatusText(): String` - Get status text ("OK", "Not Found", etc.)
- `isSuccess(): Bool` - Check if status code indicates success (200-299)
- `getBodyAsString(): String` - Get response body as string
- `getBodyAsStream(): Stream` - Get response body as stream for large responses
- `getHeader(name: String): String` - Get specific header value
- `getAllHeaders(): Map<String, String>` - Get all headers

### UrlParser

Utility class for parsing URLs and extracting components.

#### Methods

- `extractHost(url: String): String` - Extract hostname from URL
- `extractPort(url: String): Int` - Extract port number (default 80/443)
- `extractPath(url: String): String` - Extract path component
- `isHttps(url: String): Bool` - Check if URL uses HTTPS

## Building

### Prerequisites

- AmLang compiler (amlc)
- Dependencies: am-lang-core, am-net, am-ssl
- For HTTPS: OpenSSL development libraries

### Build Commands

```bash
# Build for macOS ARM
make build-macos-arm

# Build for macOS Intel
make build-macos

# Build for Linux
make build-linux-x64

# Build for AmigaOS
make build-amigaos
```

### Dependencies

This project requires the following AmLang libraries:

- **am-lang-core**: Core language features and basic types
- **am-net**: Network socket functionality
- **am-ssl**: SSL/TLS encryption support

## Examples

See the `examples/web-client-demo` directory for a complete demonstration including:

- Simple GET requests
- POST requests with form data
- HTTPS requests with SSL
- Error handling examples
- Different host connections

To run the demo:

```bash
cd examples/web-client-demo
make build-macos-arm
./builds/bin/macos-arm/app
```

## Platform Support

| Platform | HTTP | HTTPS | Notes |
|----------|------|-------|-------|
| macOS | ✅ | ✅ | Full support with OpenSSL |
| Linux | ✅ | ✅ | Full support with OpenSSL |
| AmigaOS | ✅ | ⚠️ | HTTP supported, HTTPS requires SSL setup |

## SSL/HTTPS Configuration

For HTTPS support, ensure OpenSSL is properly installed:

### macOS (Homebrew)
```bash
brew install openssl
```

### Linux (Ubuntu/Debian)
```bash
sudo apt-get install libssl-dev
```

The library automatically detects HTTPS URLs and uses SSL encryption when needed.

## Error Handling

The library provides structured error handling through HTTP status codes:

```amlang
var response = client.get("http://example.com/api")

if response.isSuccess() {
    // Handle successful response (200-299)
    var data = response.getBodyAsString()
} else {
    // Handle error response
    var statusCode = response.getStatusCode()
    if statusCode == 404 {
        "Resource not found".println()
    } else if statusCode >= 500 {
        "Server error".println()
    }
}
```

## Contributing

1. Fork the repository
2. Create your feature branch
3. Add tests for new functionality
4. Ensure all platforms build successfully
5. Submit a pull request

## License

This project is part of the AmLang ecosystem. See the main AmLang repository for license details.

## Version History

### 1.0.0
- Initial release
- Basic HTTP/HTTPS client functionality
- SSL/TLS support
- Multi-platform compatibility
- Structured response handling
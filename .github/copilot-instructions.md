# AmLang Web Client - GitHub Copilot Instructions

This file contains comprehensive information about the AmLang Web Client library to help GitHub Copilot provide accurate code suggestions and support for HTTP/HTTPS client development.

## Library Overview

AmLang Web Client is a comprehensive HTTP/HTTPS client library that provides easy-to-use web request functionality with SSL/TLS support. It's designed for AmLang applications targeting multiple platforms including AmigaOS, macOS, and Linux.

## Core Components

### WebClient Class
The main entry point for making HTTP requests.

```amlang
use Am.Web.Client

// Create client instance
var client = WebClient()

// Make requests
var getResponse = client.get("http://example.com/api")
var postResponse = client.post("http://example.com/api", "data=value")
var putResponse = client.put("http://example.com/api", "updated=data")
var deleteResponse = client.delete("http://example.com/api")
```

### HttpResponse Class
Represents HTTP responses with full access to status, headers, and body.

```amlang
// Check response status
var response = client.get("http://httpbin.org/get")
var statusCode = response.getStatusCode()  // Int: 200, 404, 500, etc.
var statusText = response.getStatusText()  // String: "OK", "Not Found", etc.
var isOk = response.isSuccess()           // Bool: true for 200-299

// Access response body
var bodyString = response.getBodyAsString()  // Full response as string
var bodyStream = response.getBodyAsStream()  // Stream for large responses

// Access headers
var contentType = response.getHeader("Content-Type")
var allHeaders = response.getAllHeaders()  // Map<String, String>
```

### URL Support
The library supports both HTTP and HTTPS URLs with automatic protocol detection.

```amlang
// HTTP requests
var httpResponse = client.get("http://httpbin.org/get")

// HTTPS requests (requires SSL support)
var httpsResponse = client.get("https://httpbin.org/get")

// Automatic port detection
// http://example.com -> port 80
// https://example.com -> port 443
// http://example.com:8080 -> port 8080
```

## Common Usage Patterns

### Basic GET Request
```amlang
use Am.Web.Client

fun fetchUserData(userId: String): String {
    var client = WebClient()
    var url = "https://api.example.com/users/" + userId
    var response = client.get(url)
    
    if response.isSuccess() {
        return response.getBodyAsString()
    } else {
        return "Error: " + response.getStatusCode().toString()
    }
}
```

### POST with Form Data
```amlang
use Am.Web.Client

fun createUser(name: String, email: String): Bool {
    var client = WebClient()
    var data = "name=" + name + "&email=" + email
    var response = client.post("https://api.example.com/users", data)
    
    return response.isSuccess()
}
```

### API Request with String Response
```amlang
use Am.Web.Client

fun fetchApiData(endpoint: String): String {
    var client = WebClient()
    var url = "https://api.example.com/" + endpoint
    var response = client.get(url)
    
    if response.isSuccess() {
        return response.getBodyAsString()
    } else {
        return "Error: " + response.getStatusCode().toString()
    }
}
```

### POST with Form Data
```amlang
use Am.Web.Client

fun robustHttpRequest(url: String): String {
    var client = WebClient()
    var response = client.get(url)
    var status = response.getStatusCode()
    
    if status >= 200 && status < 300 {
        return response.getBodyAsString()
    } else if status == 404 {
        "Resource not found".println()
        return ""
    } else if status >= 500 {
        "Server error: ".print()
        status.toString().println()
        return ""
    } else {
        "Client error: ".print()
        status.toString().println()
        return ""
    }
}
```

### Header Inspection
```amlang
use Am.Web.Client

fun inspectResponse(url: String) {
    var client = WebClient()
    var response = client.get(url)
    
    ("Status: " + response.getStatusCode().toString()).println()
    ("Content-Type: " + response.getHeader("Content-Type")).println()
    ("Content-Length: " + response.getHeader("Content-Length")).println()
    
    var headers = response.getAllHeaders()
    for var entry of headers.entrySet() {
        (entry.getKey() + ": " + entry.getValue()).println()
    }
}
```

## URL Parsing Utilities

The UrlParser class provides utilities for URL manipulation:

```amlang
use Am.Web.Client.UrlParser

var parser = UrlParser()

// Extract components
var host = parser.extractHost("https://example.com:8080/api/users")  // "example.com"
var port = parser.extractPort("https://example.com:8080/api/users")  // 8080
var path = parser.extractPath("https://example.com:8080/api/users")  // "/api/users"
var isSecure = parser.isHttps("https://example.com/api")             // true
```

## SSL/HTTPS Configuration

### Dependencies Required
The web client requires these AmLang dependencies:
- am-lang-core: Core language features
- am-net: Socket networking
- am-ssl: SSL/TLS encryption

### Platform-Specific SSL Setup

#### macOS with Homebrew
```bash
brew install openssl
```

#### Linux (Ubuntu/Debian)
```bash
sudo apt-get install libssl-dev
```

#### AmigaOS
SSL support varies by AmigaOS version and available SSL libraries.

### SSL Usage in Code
```amlang
use Am.Web.Client

// HTTPS is automatically detected and handled
var client = WebClient()
var response = client.get("https://secure.example.com/api")

// Check if SSL connection was successful
if response.isSuccess() {
    "Secure connection established".println()
    var data = response.getBodyAsString()
}
```

## Advanced Features

### Stream-Based Response Handling
For large responses, use streams to avoid memory issues:

```amlang
use Am.Web.Client

fun downloadLargeFile(url: String, outputFile: String) {
    var client = WebClient()
    var response = client.get(url)
    
    if response.isSuccess() {
        var inputStream = response.getBodyAsStream()
        var outputStream = FileStream(outputFile, FileAccess.Write)
        
        // Copy stream data
        var buffer = Array<Byte>(1024)
        var bytesRead = 0
        while (bytesRead = inputStream.read(buffer)) > 0 {
            outputStream.write(buffer, 0, bytesRead)
        }
        
        inputStream.close()
        outputStream.close()
    }
}
```

### Data Extraction from Response
```amlang
use Am.Web.Client

fun extractValueFromResponse(response: String, key: String): String {
    // Simple string-based extraction
    var searchKey = "\"" + key + "\":"
    var startIndex = response.indexOf(searchKey)
    
    if startIndex != -1 {
        var valueStart = response.indexOf("\"", startIndex + searchKey.length()) + 1
        var valueEnd = response.indexOf("\"", valueStart)
        if valueEnd != -1 {
            return response.substring(valueStart, valueEnd - valueStart)
        }
    }
    
    return ""
}

fun fetchUserData(userId: String): String {
    var client = WebClient()
    var response = client.get("https://api.example.com/users/" + userId)
    
    if response.isSuccess() {
        var responseText = response.getBodyAsString()
        var userName = extractValueFromResponse(responseText, "name")
        var userEmail = extractValueFromResponse(responseText, "email")
        
        return "Name: " + userName + ", Email: " + userEmail
    }
    
    return "Failed to fetch user data"
}
```

## Building and Dependencies

### Package Configuration (package.yml)
```yaml
dependencies:
  - id: am-lang-core
    realm: github
    type: git-repo
    tag: latest
    branch: master
    url: https://github.com/anderskjeldsen/am-lang-core.git
  - id: am-net
    type: local
    path: ../am-net
  - id: am-ssl
    type: local
    path: ../am-ssl
```

### Build Commands
```bash
# Build for current platform
make

# Build for specific platforms
make build-macos-arm
make build-linux-x64
make build-amigaos
```

## Common Troubleshooting

### SSL Certificate Issues
```amlang
// If SSL certificate validation fails, check:
// 1. System time is correct
// 2. CA certificates are up to date
// 3. OpenSSL is properly installed

var response = client.get("https://badssl.example.com")
if !response.isSuccess() {
    "SSL connection failed".println()
    response.getStatusCode().toString().println()
}
```

### Network Connectivity
```amlang
// Test basic connectivity before HTTPS
var httpResponse = client.get("http://httpbin.org/get")
if httpResponse.isSuccess() {
    "HTTP works, testing HTTPS...".println()
    var httpsResponse = client.get("https://httpbin.org/get")
    if !httpsResponse.isSuccess() {
        "HTTPS connection failed".println()
    }
}
```

### Memory Management
```amlang
// For large responses, prefer streams over strings
var response = client.get("http://example.com/largefile")
if response.isSuccess() {
    // Don't do this for large files:
    // var largeString = response.getBodyAsString()
    
    // Do this instead:
    var stream = response.getBodyAsStream()
    // Process stream in chunks
}
```

## Integration Examples

### With String Processing
```amlang
use Am.Web.Client

fun fetchAndProcessData(userId: String): String {
    var client = WebClient()
    var url = "https://api.example.com/users/" + userId
    var response = client.get(url)
    
    if response.isSuccess() {
        var responseText = response.getBodyAsString()
        // Process the response text as needed
        // Example: extract specific values using string operations
        if responseText.contains("\"status\":\"active\"") {
            return "User is active"
        } else {
            return "User status unknown"
        }
    }
    
    return "Error fetching user data"
}
```

### With File I/O
```amlang
use Am.Web.Client
use Am.IO

fun downloadToFile(url: String, filename: String): Bool {
    var client = WebClient()
    var response = client.get(url)
    
    if response.isSuccess() {
        var file = File(filename)
        var stream = file.createTextStream()
        stream.write(response.getBodyAsString())
        stream.close()
        return true
    }
    
    return false
}
```

## Best Practices

1. **Always check response status** before processing body
2. **Use HTTPS** for sensitive data transmission  
3. **Handle network errors gracefully** with appropriate user feedback
4. **Use streams** for large response bodies to manage memory
5. **Validate URLs** before making requests
6. **Process response text** using string operations for data extraction
7. **Implement retry logic** for transient network failures
8. **Set reasonable timeouts** (when timeout support is added)

## Version Information

This guide covers AmLang Web Client version 1.0.0, which provides:
- Basic HTTP/HTTPS client functionality
- SSL/TLS support via am-ssl
- Multi-platform compatibility
- Structured response handling with HttpResponse objects
- URL parsing utilities
- Cross-platform build support

For updates and additional features, check the project repository and changelog.
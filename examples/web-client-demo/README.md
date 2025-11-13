# Web Client Demo

This example demonstrates how to use the AmLang Web Client library to make HTTP requests.

## Features Demonstrated

- **Simple GET Request**: Making a basic HTTP GET request to retrieve data
- **POST Request with Data**: Sending data to a server using HTTP POST  
- **Multiple Hosts**: Making requests to different web servers
- **Response Handling**: Receiving and displaying HTTP responses

## Building and Running

1. Ensure you have the AmLang compiler (`amlc.jar`) in this directory
2. Build the example:
   ```bash
   make build
   ```
3. Run the compiled program (platform-specific)

## Code Structure

- `src/am-lang/WebClientDemo/Program.aml` - Main demonstration program
- Shows usage of the `WebClient` class from the am-web-client library

## HTTP Requests Made

The demo makes the following requests:

1. `GET http://httpbin.org/get` - Test endpoint that echoes request info
2. `POST http://httpbin.org/post` - Test endpoint for POST requests  
3. `GET http://example.com/` - Simple web page request

## Notes

- Due to a current compiler limitation, colons (`:`) in URLs are replaced with underscores (`_`)
- The web client uses basic HTTP/1.1 protocol over TCP sockets
- Responses are returned as raw HTTP response strings including headers and body

## Dependencies

- am-lang-core: Core AmLang language runtime
- am-web-client: HTTP client library (parent project)
  - am-net: Networking library for socket operations
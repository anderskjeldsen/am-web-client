# API Client Demo

This demo showcases how to use the AmLang Web Client library to interact with REST APIs, specifically demonstrating authentication with the ami-social-api Play Framework application.

## Features Demonstrated

1. **API Connection Testing** - Verify the API server is running
2. **Successful Authentication** - Login with correct credentials  
3. **Authentication Failure Handling** - Handle incorrect credentials gracefully
4. **Invalid User Handling** - Test with non-existent users
5. **HTTP Response Processing** - Parse status codes and response bodies
6. **Form Data Submission** - Send POST requests with form-encoded data

## Prerequisites

- The ami-social-api Play Framework server should be running on `http://localhost:9000`
- AmLang Web Client library with SSL support
- Network connectivity to localhost

## Running the Demo

### Build and Run
```bash
make clean && make build && make run
```

### Expected Output
The demo will show:
- Connection test to the API server
- Login attempts with different credential scenarios
- HTTP response status codes and messages
- Success/failure indicators for each test

## API Endpoints Used

- `GET /` - API connection test
- `POST /login` - User authentication
  - Parameters: `username`, `password`
  - Success: Returns JSON with `token` and `username`
  - Failure: Returns HTTP 401 Unauthorized

## Authentication Details

The demo uses these test credentials:
- **Valid User**: `kjeldsenanders@gmail.com` / `1234`
- **Invalid Users**: Various incorrect username/password combinations

## Code Highlights

- **URL Encoding**: Basic implementation for form parameters
- **HTTP Headers**: Setting Content-Type for form submissions  
- **Response Parsing**: Checking status codes and response content
- **Error Handling**: Graceful handling of authentication failures

This demo serves as a foundation for building AmLang applications that need to authenticate with REST APIs and handle HTTP responses.

## Dependencies

- am-lang-core: Core AmLang language runtime
- am-web-client: HTTP client library (parent project)
  - am-net: Networking library for socket operations
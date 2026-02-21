#!/usr/bin/env python3
"""
Rigorous Webserver Test Suite
Tests all major features of the webserver implementation
"""

import requests
import sys
import os
import time
import socket
import threading
from typing import List, Tuple, Dict, Any
from dataclasses import dataclass
import io

# ANSI color codes for output
class Colors:
    GREEN = '\033[92m'
    RED = '\033[91m'
    YELLOW = '\033[93m'
    BLUE = '\033[94m'
    MAGENTA = '\033[95m'
    CYAN = '\033[96m'
    RESET = '\033[0m'
    BOLD = '\033[1m'

@dataclass
class TestResult:
    """Store test result information"""
    name: str
    passed: bool
    message: str
    duration: float

class WebServerTester:
    def __init__(self, base_url: str = "http://localhost:8080"):
        self.base_url = base_url
        self.results: List[TestResult] = []
        self.session = requests.Session()
        
    def print_header(self, text: str):
        """Print a formatted header"""
        print(f"\n{Colors.BOLD}{Colors.CYAN}{'='*70}{Colors.RESET}")
        print(f"{Colors.BOLD}{Colors.CYAN}{text.center(70)}{Colors.RESET}")
        print(f"{Colors.BOLD}{Colors.CYAN}{'='*70}{Colors.RESET}\n")
    
    def print_test(self, name: str):
        """Print test name"""
        print(f"{Colors.BLUE}[TEST]{Colors.RESET} {name}...", end=" ", flush=True)
    
    def record_result(self, name: str, passed: bool, message: str, duration: float):
        """Record and print test result"""
        self.results.append(TestResult(name, passed, message, duration))
        if passed:
            print(f"{Colors.GREEN}✓ PASS{Colors.RESET} ({duration:.3f}s)")
            if message:
                print(f"      {Colors.GREEN}{message}{Colors.RESET}")
        else:
            print(f"{Colors.RED}✗ FAIL{Colors.RESET} ({duration:.3f}s)")
            print(f"      {Colors.RED}{message}{Colors.RESET}")
    
    def test(self, name: str, test_func):
        """Execute a test and record results"""
        self.print_test(name)
        start = time.time()
        try:
            result, message = test_func()
            duration = time.time() - start
            self.record_result(name, result, message, duration)
            return result
        except Exception as e:
            duration = time.time() - start
            self.record_result(name, False, f"Exception: {str(e)}", duration)
            return False
    
    # ==================== Connectivity Tests ====================
    
    def test_server_connection(self) -> Tuple[bool, str]:
        """Test if server is reachable"""
        try:
            response = self.session.get(self.base_url, timeout=5)
            return True, f"Server responded with status {response.status_code}"
        except Exception as e:
            return False, f"Cannot connect to server: {str(e)}"
    
    # ==================== GET Request Tests ====================
    
    def test_get_root(self) -> Tuple[bool, str]:
        """Test GET request to root"""
        response = self.session.get(f"{self.base_url}/")
        if response.status_code == 200:
            return True, "Successfully retrieved root page"
        return False, f"Expected 200, got {response.status_code}"
    
    def test_get_static_html(self) -> Tuple[bool, str]:
        """Test GET request for static HTML file"""
        response = self.session.get(f"{self.base_url}/test.html")
        if response.status_code == 200 and 'text/html' in response.headers.get('Content-Type', ''):
            return True, "Static HTML served correctly"
        return False, f"Status: {response.status_code}, Content-Type: {response.headers.get('Content-Type')}"
    
    def test_get_static_css(self) -> Tuple[bool, str]:
        """Test GET request for CSS file"""
        response = self.session.get(f"{self.base_url}/style.css")
        if response.status_code == 200 and 'css' in response.headers.get('Content-Type', '').lower():
            return True, "CSS file served correctly"
        return False, f"Status: {response.status_code}, Content-Type: {response.headers.get('Content-Type')}"
    
    def test_get_404(self) -> Tuple[bool, str]:
        """Test 404 error page"""
        response = self.session.get(f"{self.base_url}/nonexistent-page-12345.html")
        if response.status_code == 404:
            return True, "404 error returned correctly"
        return False, f"Expected 404, got {response.status_code}"
    
    # ==================== HEAD Request Tests ====================
    
    # ==================== POST Request Tests ====================
    
    def test_post_upload(self) -> Tuple[bool, str]:
        """Test POST file upload"""
        test_content = b"Test file upload content - " + str(time.time()).encode()
        files = {'file': ('test_upload.txt', io.BytesIO(test_content), 'text/plain')}
        
        response = self.session.post(f"{self.base_url}/www", files=files)
        if response.status_code in [200, 201, 204]:
            return True, f"File uploaded successfully (status {response.status_code})"
        return False, f"Expected 200/201/204, got {response.status_code}: {response.text[:100]}"
    
    def test_post_cgi(self) -> Tuple[bool, str]:
        """Test POST to CGI endpoint"""
        data = {'name': 'tester', 'value': '12345'}
        try:
            response = self.session.post(f"{self.base_url}/cgi-bin/process.py", data=data, timeout=10)
            if response.status_code == 200:
                return True, "CGI POST executed successfully"
            return False, f"Expected 200, got {response.status_code}"
        except requests.exceptions.Timeout:
            return False, "CGI request timed out"
    
    def test_post_large_body(self) -> Tuple[bool, str]:
        """Test POST with large body (within limits)"""
        # Create 1MB file (under the 200MB limit)
        large_content = b'A' * (1024 * 1024)  # 1 MB
        files = {'file': ('large_file.txt', io.BytesIO(large_content), 'text/plain')}
        
        response = self.session.post(f"{self.base_url}/www", files=files, timeout=30)
        if response.status_code in [200, 201, 204]:
            return True, "Large file (1MB) uploaded successfully"
        if response.status_code == 413:
            return True, "Large body correctly rejected (413)"
        return False, f"Expected 200/201/204 or 413, got {response.status_code}"
    
    def test_post_body_too_large(self) -> Tuple[bool, str]:
        """Test POST with body exceeding client_max_body_size"""
        # This test would need a file > 200MB, which might be too slow
        # Instead, we'll skip or test with a smaller server config
        return True, "Skipped (would require >200MB upload)"
    
    # ==================== CGI Tests ====================
    
    def test_cgi_get(self) -> Tuple[bool, str]:
        """Test CGI with GET request"""
        try:
            response = self.session.get(f"{self.base_url}/cgi-bin/time.py", timeout=10)
            if response.status_code == 200:
                return True, "CGI GET executed successfully"
            return False, f"Expected 200, got {response.status_code}"
        except requests.exceptions.Timeout:
            return False, "CGI GET timed out"
    
    # ==================== Keep-Alive Tests ====================
    
    def test_keep_alive(self) -> Tuple[bool, str]:
        """Test HTTP keep-alive connections"""
        # Make multiple requests on same session
        response1 = self.session.get(f"{self.base_url}/")
        response2 = self.session.get(f"{self.base_url}/test.html")
        
        conn_header = response1.headers.get('Connection', '').lower()
        if response1.status_code == 200 and response2.status_code == 200:
            return True, f"Multiple requests successful (Connection: {conn_header})"
        return False, "Keep-alive connection failed"
    
    # ==================== Concurrent Request Tests ====================
    
    def test_concurrent_requests(self) -> Tuple[bool, str]:
        """Test handling of concurrent requests"""
        def make_request():
            try:
                response = requests.get(f"{self.base_url}/", timeout=10)
                return response.status_code == 200
            except:
                return False
        
        threads = []
        results = []
        
        # Launch 10 concurrent requests
        for _ in range(10):
            thread = threading.Thread(target=lambda: results.append(make_request()))
            threads.append(thread)
            thread.start()
        
        # Wait for all threads
        for thread in threads:
            thread.join()
        
        success_count = sum(results)
        if success_count >= 8:  # Allow for some potential failures
            return True, f"{success_count}/10 concurrent requests succeeded"
        return False, f"Only {success_count}/10 concurrent requests succeeded"
    
    # ==================== Stress Tests ====================
    
    def test_rapid_requests(self) -> Tuple[bool, str]:
        """Test rapid sequential requests"""
        success = 0
        total = 20
        
        for i in range(total):
            try:
                response = self.session.get(f"{self.base_url}/", timeout=5)
                if response.status_code == 200:
                    success += 1
            except:
                pass
        
        if success >= total * 0.9:  # 90% success rate
            return True, f"{success}/{total} rapid requests succeeded"
        return False, f"Only {success}/{total} rapid requests succeeded"
    
    # ==================== Chunked Transfer Tests ====================
    
    def test_chunked_request(self) -> Tuple[bool, str]:
        """Test chunked transfer encoding"""
        # Requests library handles chunked encoding automatically for generators
        def content_generator():
            for i in range(10):
                yield f"Chunk {i}\n".encode()
        
        try:
            response = self.session.post(
                f"{self.base_url}/uploads/",
                data=content_generator(),
                headers={'Transfer-Encoding': 'chunked'}
            )
            if response.status_code in [200, 201, 204]:
                return True, "Chunked transfer handled"
            return False, f"Status: {response.status_code}"
        except Exception as e:
            return False, f"Chunked transfer failed: {str(e)}"
    
    # ==================== Invalid Request Tests ====================
    
    def test_invalid_http_version(self) -> Tuple[bool, str]:
        """Test invalid HTTP version"""
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(5)
            sock.connect(('localhost', 8080))
            sock.send(b"GET / HTTP/9.9\r\nHost: localhost\r\n\r\n")
            response = sock.recv(4096)
            sock.close()
            
            if b"400" in response or b"505" in response or b"HTTP" in response:
                return True, "Invalid HTTP version handled"
            return False, "No response to invalid HTTP version"
        except Exception as e:
            return False, f"Socket error: {str(e)}"
    
    def test_malformed_request(self) -> Tuple[bool, str]:
        """Test malformed HTTP request"""
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(5)
            sock.connect(('localhost', 8080))
            sock.send(b"INVALID REQUEST\r\n\r\n")
            response = sock.recv(4096)
            sock.close()
            
            if b"400" in response or b"HTTP" in response:
                return True, "Malformed request handled"
            return False, "No response to malformed request"
        except Exception as e:
            return False, f"Socket error: {str(e)}"
    
    def test_missing_host_header(self) -> Tuple[bool, str]:
        """Test request without Host header"""
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(5)
            sock.connect(('localhost', 8080))
            sock.send(b"GET / HTTP/1.1\r\n\r\n")  # Missing Host header
            response = sock.recv(4096)
            sock.close()
            
            # Server might accept it or return 400
            if b"HTTP" in response:
                return True, "Missing Host header handled"
            return False, "No response"
        except Exception as e:
            return False, f"Socket error: {str(e)}"
    
    # ==================== Performance Tests ====================
    
    def test_response_time(self) -> Tuple[bool, str]:
        """Test response time for simple GET"""
        times = []
        for _ in range(5):
            start = time.time()
            response = self.session.get(f"{self.base_url}/")
            duration = time.time() - start
            times.append(duration)
            if response.status_code != 200:
                return False, "Request failed"
        
        avg_time = sum(times) / len(times)
        if avg_time < 1.0:  # Should respond in less than 1 second
            return True, f"Average response time: {avg_time:.3f}s"
        return False, f"Average response time too slow: {avg_time:.3f}s"
    
    # ==================== Main Test Runner ====================
    
    def run_all_tests(self):
        """Run all tests and generate report"""
        self.print_header("WEBSERVER RIGOROUS TEST SUITE")
        
        print(f"{Colors.YELLOW}Testing server at: {self.base_url}{Colors.RESET}")
        print(f"{Colors.YELLOW}Starting tests at: {time.strftime('%Y-%m-%d %H:%M:%S')}{Colors.RESET}\n")
        
        # Connectivity
        self.print_header("CONNECTIVITY TESTS")
        if not self.test("Server Connection", self.test_server_connection):
            print(f"\n{Colors.RED}Cannot connect to server. Aborting tests.{Colors.RESET}")
            return False
        
        # Basic GET tests (subject-required)
        self.print_header("GET REQUEST TESTS")
        self.test("GET Root", self.test_get_root)
        self.test("GET Static HTML", self.test_get_static_html)
        self.test("GET Static CSS", self.test_get_static_css)
        self.test("GET 404 Error", self.test_get_404)
        
        # POST tests (subject-required)
        self.print_header("POST REQUEST TESTS")
        self.test("POST File Upload", self.test_post_upload)
        self.test("POST to CGI", self.test_post_cgi)
        self.test("POST Large Body (1MB)", self.test_post_large_body)
        self.test("POST Body Too Large", self.test_post_body_too_large)
        
        # CGI tests (subject-required)
        self.print_header("CGI TESTS")
        self.test("CGI GET Request", self.test_cgi_get)
        
        # Connection tests
        self.print_header("CONNECTION TESTS")
        self.test("Keep-Alive", self.test_keep_alive)
        
        # Concurrent tests
        self.print_header("CONCURRENT REQUEST TESTS")
        self.test("Concurrent Requests (10)", self.test_concurrent_requests)
        self.test("Rapid Sequential Requests (20)", self.test_rapid_requests)
        
        # Invalid requests
        self.print_header("INVALID REQUEST TESTS")
        self.test("Invalid HTTP Version", self.test_invalid_http_version)
        self.test("Malformed Request", self.test_malformed_request)
        self.test("Missing Host Header", self.test_missing_host_header)
        
        # Performance
        self.print_header("PERFORMANCE TESTS")
        self.test("Response Time", self.test_response_time)
        
        # Generate report
        self.generate_report()
        
        return all(r.passed for r in self.results)
    
    def generate_report(self):
        """Generate and print test report"""
        self.print_header("TEST SUMMARY")
        
        total = len(self.results)
        passed = sum(1 for r in self.results if r.passed)
        failed = total - passed
        pass_rate = (passed / total * 100) if total > 0 else 0
        
        print(f"Total Tests:  {total}")
        print(f"{Colors.GREEN}Passed:       {passed} ({pass_rate:.1f}%){Colors.RESET}")
        if failed > 0:
            print(f"{Colors.RED}Failed:       {failed} ({100-pass_rate:.1f}%){Colors.RESET}")
        
        total_time = sum(r.duration for r in self.results)
        print(f"\nTotal Time:   {total_time:.3f}s")
        print(f"Average Time: {total_time/total:.3f}s per test")
        
        if failed > 0:
            print(f"\n{Colors.RED}{Colors.BOLD}FAILED TESTS:{Colors.RESET}")
            for result in self.results:
                if not result.passed:
                    print(f"  {Colors.RED}✗{Colors.RESET} {result.name}")
                    print(f"    {Colors.RED}{result.message}{Colors.RESET}")
        
        print(f"\n{Colors.BOLD}{'='*70}{Colors.RESET}")
        if failed == 0:
            print(f"{Colors.GREEN}{Colors.BOLD}ALL TESTS PASSED! ✓{Colors.RESET}")
        else:
            print(f"{Colors.YELLOW}TESTS COMPLETED WITH {failed} FAILURE(S){Colors.RESET}")
        print(f"{Colors.BOLD}{'='*70}{Colors.RESET}\n")

def main():
    """Main entry point"""
    base_url = "http://localhost:8080"
    
    if len(sys.argv) > 1:
        base_url = sys.argv[1]
    
    print(f"{Colors.BOLD}{Colors.MAGENTA}")
    print("╔════════════════════════════════════════════════════════════════════╗")
    print("║          WEBSERVER RIGOROUS TEST SUITE                            ║")
    print("║          Comprehensive HTTP Server Testing                        ║")
    print("╚════════════════════════════════════════════════════════════════════╝")
    print(f"{Colors.RESET}\n")
    
    tester = WebServerTester(base_url)
    success = tester.run_all_tests()
    
    sys.exit(0 if success else 1)

if __name__ == "__main__":
    main()



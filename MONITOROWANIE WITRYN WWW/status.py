import sys
import http.client
import urllib.parse

EXPECTED_CONTENT_TYPE = "text/html"

def get_status(url, req_string):
    try:
        parsed_url = urllib.parse.urlparse(url)

        if parsed_url.scheme not in ('http', 'https'):
            raise Exception(f"Unsupported scheme: {parsed_url.scheme}")

        host = parsed_url.hostname
        path = parsed_url.path or '/'
        use_https = parsed_url.scheme == 'https'

        if use_https:
            connection = http.client.HTTPSConnection(host, timeout=10)
        else:
            connection = http.client.HTTPConnection(host, timeout=10)

        connection.request("GET", path)
        response = connection.getresponse()

        if response.status != http.client.OK:
            raise Exception(f"Request failed with status: {response.status}")

        content_type = response.getheader("Content-Type", "")
        if EXPECTED_CONTENT_TYPE not in content_type:
            raise Exception(f"Wrong Content-Type {content_type}")

        content = response.read().decode("utf-8")
        # print(content)
        if req_string not in content:
            raise Exception(f"Request string {req_string} not found")

        print("Everything is OK")
        sys.exit(0)

    except Exception as err:
        print(f"Error: {err}")
        sys.exit(1)

    finally:
        try:
            connection.close()
        except:
            pass

if __name__ == "__main__":
    get_status("http://th.if.uj.edu.pl/", "Institute of Theoretical Physics")

import json
import sys
import http.client

HOST = "api.discogs.com"

def get_artist(artist_id):
    try:
        conn = http.client.HTTPSConnection(HOST, timeout=10)
        url = f"/artists/{artist_id}"
        headers = {
            "User-Agent": "DiscogsBandMateFinder/0.1 +http://cokolwiek.local"
        }

        conn.request("GET", url, headers=headers)
        response = conn.getresponse()
        status = response.status

        if status != 200:
            raise Exception(f"Status code: {status}")

        data = response.read()
        data = data.decode("utf-8")
        conn.close()

        return json.loads(data)
    except Exception as e:
        print(f"Error getting artist {artist_id}: {e}")
        return None

def get_artist_groups(artist_data):
    groups = set()
    for artist_group in artist_data.get('groups', []):
        group_id = artist_group.get('id')
        group_name = artist_group.get('name')
        if group_id and group_name:
            groups.add((group_id, group_name))
    return groups

def main():
    if len(sys.argv) < 2:
        print("Usage: artist_finder.py <artist_id>")
        sys.exit(1)

    artist_ids = sys.argv[1:]

    artist_to_name = {}
    group_to_artists = {}

    for artist_id in artist_ids:
        data = get_artist(artist_id)
        #print(data)
        name = data.get("name", f"(unknown {data.get('id', '')})")
        artist_to_name[artist_id] = name
        groups = get_artist_groups(data)
        for group_id, group_name in groups:
            key = (group_id, group_name)
            if key not in group_to_artists:
                group_to_artists[key] = set()
            group_to_artists[key].add(artist_id)

    print('Common groups:')

    common_groups = {
        group: ids for (group, ids) in group_to_artists.items() if len(ids) > 1
    }

    if not common_groups:
        print("No common groups found.")
        return

    for (group_id, group_name) in sorted(common_groups.keys(), key=lambda g: g[1]):
        artist_names = sorted(artist_to_name[aid] for aid in common_groups[(group_id, group_name)])
        print(f"{group_name} (id {group_id}):")
        for name in artist_names:
            print(f"  - {name}")

if __name__ == '__main__':
    main()
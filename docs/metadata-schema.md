# Cuelet Metadata Schema

Cuelet library metadata lives inside the selected library folder:

```text
.cuelet-metadata.json
```

The Linux GTK app writes schema version `2`. It can read the older Qt prototype shape where `version` is missing or `1` and sound metadata lives under `sounds`.

## Version 2 Shape

```json
{
  "version": 2,
  "categories": [
    {
      "id": "user-fx-123456",
      "name": "FX",
      "color": "#3478F6",
      "icon": "tag-symbolic",
      "editable": true
    }
  ],
  "sounds": {
    "fx/impact.wav": {
      "displayName": "Impact",
      "title": "Impact",
      "categoryId": "user-fx-123456",
      "category": "FX",
      "favorite": true,
      "notes": "Layered impact",
      "aliases": ["boom", "slam"],
      "shortcut": {
        "keyval": 120,
        "modifiers": 5,
        "label": "Ctrl+Shift+X"
      },
      "addedAt": 1783180800,
      "lastPlayedAt": 1783180900
    }
  }
}
```

## Field Notes

- `sounds` keys are relative paths using `/` separators.
- `displayName` is the user-facing sound name.
- `title` is written as a compatibility alias for the older Qt metadata file.
- `categoryId` is the stable category identifier.
- `category` is written as a compatibility display name for older readers.
- `favorite` persists the favorite state.
- `notes` and `aliases` are searchable.
- `shortcut` stores structured key data. On Linux, `keyval` is the GDK key value and `modifiers` is the GDK modifier mask.
- `addedAt` and `lastPlayedAt` are Unix timestamps.

## Built-In Category

`uncategorized` is built in and not stored in the `categories` array unless a reader chooses to materialize it in memory:

```json
{
  "id": "uncategorized",
  "name": "Uncategorized",
  "color": "#8E8E93",
  "icon": "folder-symbolic",
  "editable": false
}
```

## Migration

When Linux loads a version `1` or unversioned Qt metadata file and later saves, it writes a version `2` file. Before overwriting, it creates a conservative backup next to the original:

```text
.cuelet-metadata.json.v1.bak
```

Older Qt fields are mapped as follows:

- `title` -> `displayName`
- `category` -> stable user category and `categoryId`
- `favorite` -> `favorite`
- `icon` is preserved only indirectly through category definitions when available
- `notes` -> `notes`
- `aliases` -> `aliases`

## macOS Compatibility Status

The current macOS SwiftUI app has not been changed in this Linux pass. It still stores per-sound assignments in Application Support settings keyed by file URL path. Version `2` is the shared in-library schema Linux now uses and macOS should adopt next.

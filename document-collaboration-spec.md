# Document Collaboration Feature - API & Integration Spec

## Overview

The document collaboration feature allows authenticated users to open and edit supported document types in an online editor (Collabora-based). The feature involves three concerns:

1. **Fetching supported file types** from the server
2. **Generating a collaboration editor link** for a specific file
3. **Opening the editor** in an embedded browser/webview with appropriate parameters

---

## API 1: Get Supported File Extensions and MIME Types

### Endpoint
`docsrv/supportedfiletypes`

### Authentication
Requires an authenticated user (auth token).

### Request Parameters
None (beyond authentication).

### Response (success)
```json
{
  "result": 0,
  "filetypes": {
    "epub": "application/epub+zip",
    "csv": "text/csv",
    "oform": "application/vnd.openxmlformats-officedocument.wordprocessingml.document",
    "xlsb": "application/vnd.ms-excel.sheet.binary.macroenabled.12",
    "htm": "text/html"
  },
  "bymimetype": {
    "application/vnd.ms-word.template.macroEnabled.12": [
      "dotm"
    ]
  }
}
```

#### Response fields

| Field        | Type                       | Description |
|--------------|----------------------------|-------------|
| `filetypes`  | `Map<string, string>`      | Keys are file extensions, values are the corresponding MIME types. |
| `bymimetype` | `Map<string, string[]>`    | Reverse lookup -- keys are MIME types, values are arrays of extensions that map to that MIME type. |

### Error Codes

| Code | Message | Notes |
|------|---------|-------|
| 2266 | "Access denied." | User has the collaboration flag disabled. |

### Client-Side Behavior
- The set of supported extensions (the **keys** of `filetypes`) should be cached locally.
- On app startup, load the cached value first, then fetch an update from the API.
- If the fetched set differs from the cached one, update the cache.
- Use the set to determine whether the "Edit" action should be shown for a given file.
- The `bymimetype` reverse map may be useful if matching by MIME type is more convenient on a given platform.

---

## API 2: Generate Document Collaboration Link

### Endpoint
`docsrv/getdocumentcode`

### Authentication
Requires an authenticated user. Auth token as cookie or parameter.

### Request Parameters

| Parameter | Type   | Required | Default | Description |
|-----------|--------|----------|---------|-------------|
| `fileid`  | `long` | Yes      | --      | The ID of the file to be viewed/edited. |
| `os`      | `int`  | Yes      | --      | OS/platform code. `1` = Android, `3` = iOS, `4` = Web. Pick or request an appropriate value for desktop. |
| `mode`    | `int`  | No       | `1`     | Editing mode. `1` = EDIT, `2` = VIEW ONLY. |

### Response (success)
```json
{
  "result": 0,
  "link": "https://edocs.pcloud.com/editor/J049R07Z7ZV6XF7Z7Zab3f5a23ba09ecfd1b54c4d685a78b4ed428a1b1/"
}
```

- `link` (`string`): A URL to the online document editor pre-configured for the requested file and mode.

### Error Codes

| Code | Message | Notes |
|------|---------|-------|
| 1029 | "Please provide 'fileid'." | Missing required parameter. |
| 1021 | "Language not supported." | Invalid language parameter on the editor URL. |
| 2003 | "Access denied. You do not have permissions to perform this operation." | User lacks permission on the file. |
| 2009 | "File not found." | File was deleted or access was revoked. |
| 2075 | "You are not a member of a business account." | Collaboration requires a business account. |
| 2266 | "Access denied." | User has the collaboration flag disabled. |
| 5000 | "Internal error. Try again later." | Server-side error. |

---

## Editor URL Construction (Client-Side)

Once the `link` is obtained from the API, the client appends parameters before loading it.

### Query parameter form
```
{link}?lang={language}&theme={dark|light}&gobackurl={base64_encoded_redirect_uri}
```

### Path segment form (alternative, equivalent)
```
{link}/{lang}/{theme}/{auth}/
```

Both forms are supported by the editor. Query parameters and path segments can be mixed.

### Parameters

| Parameter      | Type     | Required | Description |
|----------------|----------|----------|-------------|
| `lang`         | `string` | No       | Language code (e.g. `en`, `bg`, `de`). Derived from user/system locale. |
| `theme`        | `string` | No       | `"dark"` or `"light"`. Match the host application's current theme. |
| `auth`         | `string` | No       | Auth token. Can also be provided as a cookie named `pcauth`. |
| `gobackurl`    | `string` | No       | Base64-encoded URL. The editor's "back" button will navigate here. Use a custom URI scheme or callback URL that the host app can intercept to close the editor. |
| `forcemode`    | `string` | No       | `"edit"` or `"view"` -- overrides the mode set in `getdocumentcode`. Priority order: `getdocumentcode` > `forcemode` > `cookie['mode']` > server security check. |
| `forcedisplay` | `string` | No       | `"mobile"` or `"desktop"` -- overrides the editor's auto-detected display mode. Use `"desktop"` for desktop clients. |

---

## Embedded Browser / WebView Requirements

The component used to render the editor must support:
- **JavaScript** execution
- **Cookies** (set auth cookie `pcauth` before loading the URL)
- **LocalStorage**

---

## Error Handling

Recommended client-side mapping of API errors to user-facing behavior:

| Error Code | Suggested Behavior |
|------------|-------------|
| 2003 | "Access denied" -- offer fallback to open the file with a local editor if available. |
| 2009 | "File not found" -- inform the user and close. |
| 2266 | "Collaboration not available" -- offer fallback to a local editor. |
| 2075 | "Requires a business account" -- inform the user and close. |
| 5000 | "Server error, try again later." |

---

## Flow Summary

```
1. Client checks if file extension is in the cached supported file types set
   -> If yes, show "Edit in browser" option

2. User triggers edit
   -> Show loading state

3. Client calls: docsrv/getdocumentcode(fileid=<id>, os=<platform_code>, mode=1)
   -> On success: receives editor link
   -> On error: show appropriate error message

4. Client constructs final URL:
   {link}?lang={locale}&theme={dark|light}&gobackurl={base64(callback_uri)}&forcedisplay=desktop

5. Client sets pcauth cookie, loads URL in embedded browser

6. User edits document

7. User exits via:
   - Editor's back button -> navigates to gobackurl -> host app intercepts and closes editor
   - Host app's own close/back mechanism -> close the embedded browser
```
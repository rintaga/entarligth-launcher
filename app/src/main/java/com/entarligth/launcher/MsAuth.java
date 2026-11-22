package com.entarligth.launcher;

import android.content.Context;
import android.content.SharedPreferences;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.*;
import java.net.HttpURLConnection;
import java.net.URL;
import java.net.URLEncoder;

/** Login Microsoft -> Xbox Live -> XSTS -> Minecraft (device code flow). */
final class MsAuth {
    private static final String BASE = "https://login.microsoftonline.com/consumers/oauth2/v2.0";
    private static final String SCOPE = "XboxLive.signin offline_access";

    static final class DeviceCode { String deviceCode, userCode, uri; int interval, expiresIn; }
    static final class Account { String name, uuid, token; }

    private MsAuth() {}

    private static SharedPreferences prefs(Context c) {
        return c.getSharedPreferences("auth", Context.MODE_PRIVATE);
    }

    static DeviceCode startDeviceLogin() throws Exception {
        if (BuildConfig.MS_CLIENT_ID.isEmpty())
            throw new IOException("MS_CLIENT_ID belum diisi (GitHub Variables)");
        JSONObject j = form(BASE + "/devicecode", "client_id", BuildConfig.MS_CLIENT_ID, "scope", SCOPE);
        if (!j.has("device_code")) throw new IOException(j.toString());
        DeviceCode d = new DeviceCode();
        d.deviceCode = j.getString("device_code");
        d.userCode = j.getString("user_code");
        d.uri = j.optString("verification_uri", "https://www.microsoft.com/link");
        d.interval = Math.max(2, j.optInt("interval", 5));
        d.expiresIn = j.optInt("expires_in", 900);
        return d;
    }

    static Account finishDeviceLogin(Context c, DeviceCode d) throws Exception {
        long end = System.currentTimeMillis() + d.expiresIn * 1000L;
        while (System.currentTimeMillis() < end) {
            Thread.sleep(d.interval * 1000L);
            JSONObject j = form(BASE + "/token",
                    "grant_type", "urn:ietf:params:oauth:grant-type:device_code",
                    "client_id", BuildConfig.MS_CLIENT_ID, "device_code", d.deviceCode);
            if (j.has("access_token")) return minecraft(c, j);
            String err = j.optString("error");
            if (!err.equals("authorization_pending") && !err.equals("slow_down"))
                throw new IOException(err + ": " + j.optString("error_description"));
        }
        throw new IOException("Kode kedaluwarsa");
    }

    /** Login ulang diam-diam dari refresh token; null kalau gagal. */
    static Account restore(Context c) {
        String rt = prefs(c).getString("rt", null);
        if (rt == null || BuildConfig.MS_CLIENT_ID.isEmpty()) return null;
        try {
            return minecraft(c, form(BASE + "/token", "grant_type", "refresh_token",
                    "client_id", BuildConfig.MS_CLIENT_ID, "refresh_token", rt, "scope", SCOPE));
        } catch (Exception e) { return null; }
    }

    private static Account minecraft(Context c, JSONObject ms) throws Exception {
        if (!ms.has("access_token")) throw new IOException(ms.toString());
        if (ms.has("refresh_token"))
            prefs(c).edit().putString("rt", ms.getString("refresh_token")).apply();

        JSONObject xblReq = new JSONObject()
                .put("Properties", new JSONObject().put("AuthMethod", "RPS")
                        .put("SiteName", "user.auth.xboxlive.com")
                        .put("RpsTicket", "d=" + ms.getString("access_token")))
                .put("RelyingParty", "http://auth.xboxlive.com").put("TokenType", "JWT");
        JSONObject xbl = json("https://user.auth.xboxlive.com/user/authenticate", xblReq);
        need(xbl, "Token");

        JSONObject xstsReq = new JSONObject()
                .put("Properties", new JSONObject().put("SandboxId", "RETAIL")
                        .put("UserTokens", new JSONArray().put(xbl.getString("Token"))))
                .put("RelyingParty", "rp://api.minecraftservices.com/").put("TokenType", "JWT");
        JSONObject xsts = json("https://xsts.auth.xboxlive.com/xsts/authorize", xstsReq);
        need(xsts, "Token");
        String uhs = xsts.getJSONObject("DisplayClaims").getJSONArray("xui")
                .getJSONObject(0).getString("uhs");

        JSONObject mc = json("https://api.minecraftservices.com/authentication/login_with_xbox",
                new JSONObject().put("identityToken", "XBL3.0 x=" + uhs + ";" + xsts.getString("Token")));
        need(mc, "access_token");

        JSONObject prof = http("GET", "https://api.minecraftservices.com/minecraft/profile",
                null, null, mc.getString("access_token"));
        need(prof, "id"); // gagal di sini = akun belum punya Minecraft Java

        Account a = new Account();
        a.token = mc.getString("access_token");
        a.uuid = prof.getString("id");
        a.name = prof.getString("name");
        return a;
    }

    private static void need(JSONObject j, String key) throws IOException {
        if (!j.has(key)) throw new IOException(j.toString());
    }

    private static JSONObject form(String url, String... kv) throws IOException {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < kv.length; i += 2) {
            if (i > 0) sb.append('&');
            sb.append(URLEncoder.encode(kv[i], "UTF-8")).append('=')
              .append(URLEncoder.encode(kv[i + 1], "UTF-8"));
        }
        return http("POST", url, "application/x-www-form-urlencoded", sb.toString(), null);
    }

    private static JSONObject json(String url, JSONObject body) throws IOException {
        return http("POST", url, "application/json", body.toString(), null);
    }

    private static JSONObject http(String method, String url, String type, String body, String bearer)
            throws IOException {
        HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
        c.setRequestMethod(method);
        c.setConnectTimeout(20000);
        c.setReadTimeout(30000);
        c.setRequestProperty("Accept", "application/json");
        if (bearer != null) c.setRequestProperty("Authorization", "Bearer " + bearer);
        if (body != null) {
            c.setDoOutput(true);
            c.setRequestProperty("Content-Type", type);
            try (OutputStream os = c.getOutputStream()) { os.write(body.getBytes("UTF-8")); }
        }
        int code = c.getResponseCode();
        InputStream in = code >= 400 ? c.getErrorStream() : c.getInputStream();
        String s = in == null ? "{}" : read(in);
        try { return new JSONObject(s); }
        catch (JSONException e) { throw new IOException("HTTP " + code + ": " + s); }
    }

    private static String read(InputStream in) throws IOException {
        ByteArrayOutputStream bo = new ByteArrayOutputStream();
        byte[] b = new byte[8192];
        int n;
        while ((n = in.read(b)) > 0) bo.write(b, 0, n);
        return bo.toString("UTF-8");
    }
}

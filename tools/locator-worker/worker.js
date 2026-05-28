const TTL_SECONDS = 15 * 60;

function json(data, status = 200) {
  return new Response(JSON.stringify(data), {
    status,
    headers: {
      "content-type": "application/json; charset=utf-8",
      "cache-control": "no-store",
    },
  });
}

function html(body, status = 200) {
  return new Response(body, {
    status,
    headers: {
      "content-type": "text/html; charset=utf-8",
      "cache-control": "no-store",
    },
  });
}

function publicIp(request) {
  return request.headers.get("CF-Connecting-IP") ||
    request.headers.get("X-Forwarded-For")?.split(",")[0]?.trim() ||
    "";
}

function isPrivateIpv4(ip) {
  const parts = ip.split(".").map((part) => Number(part));
  if (parts.length !== 4 || parts.some((part) => !Number.isInteger(part) || part < 0 || part > 255)) {
    return false;
  }
  return parts[0] === 10 ||
    (parts[0] === 172 && parts[1] >= 16 && parts[1] <= 31) ||
    (parts[0] === 192 && parts[1] === 168);
}

function cleanId(value) {
  const id = String(value || "").toUpperCase().replace(/[^A-F0-9]/g, "");
  return id.length >= 8 && id.length <= 16 ? id : "";
}

async function readRecord(env, key) {
  const raw = await env.AURAX_LOCATOR.get(key);
  if (!raw) return null;
  try {
    return JSON.parse(raw);
  } catch {
    return null;
  }
}

async function register(request, env) {
  const data = await request.json().catch(() => null);
  if (!data) return json({ ok: false, error: "bad_json" }, 400);

  const id = cleanId(data.id);
  const localIp = String(data.localIp || data.ip || "");
  if (!id) return json({ ok: false, error: "bad_id" }, 400);
  if (!isPrivateIpv4(localIp)) return json({ ok: false, error: "bad_local_ip" }, 400);

  const record = {
    id,
    localIp,
    hostname: String(data.hostname || "aurax").slice(0, 32),
    publicIp: publicIp(request),
    updatedAt: Date.now(),
  };
  const value = JSON.stringify(record);

  await env.AURAX_LOCATOR.put(`device:${id}`, value, { expirationTtl: TTL_SECONDS });
  if (record.publicIp) {
    await env.AURAX_LOCATOR.put(`client:${record.publicIp}`, value, { expirationTtl: TTL_SECONDS });
  }

  return json({
    ok: true,
    url: `${new URL(request.url).origin}/${id}`,
    localIp,
  });
}

function redirectTo(record) {
  return Response.redirect(`http://${record.localIp}/`, 302);
}

function notFoundPage(origin, id = "") {
  const code = id ? `<p>Zarizeni <b>${id}</b> zatim neni online, nebo se jeho zaznam obnovuje.</p>` : "";
  return html(`<!doctype html>
<html lang="cs">
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>AuraX locator</title>
<style>
body{font-family:system-ui,sans-serif;max-width:560px;margin:40px auto;padding:0 18px;line-height:1.45;background:#171717;color:#e8e2dc}
a{color:#f09a56}.box{border:1px solid #333;background:#242424;border-radius:8px;padding:18px}
code{background:#111;padding:2px 5px;border-radius:4px}
</style>
<h1>AuraX</h1>
<div class="box">
${code}
<p>Zapni hotspot, pockej par sekund po pripojeni AuraX a obnov tuto stranku.</p>
<p>Pokud mas kod zarizeni, otevri adresu <code>${origin}/KOD-ZARIZENI</code>.</p>
</div>
</html>`, 404);
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);

    if (!env.AURAX_LOCATOR) {
      return json({ ok: false, error: "missing_kv_binding" }, 500);
    }

    if (url.pathname === "/api/register" && request.method === "POST") {
      return register(request, env);
    }

    if (url.pathname === "/api/health") {
      return json({ ok: true });
    }

    const id = cleanId(url.pathname.replace(/^\/+|\/+$/g, ""));
    const key = id ? `device:${id}` : `client:${publicIp(request)}`;
    const record = key.endsWith(":") ? null : await readRecord(env, key);
    if (record?.localIp && isPrivateIpv4(record.localIp)) return redirectTo(record);

    return notFoundPage(url.origin, id);
  },
};

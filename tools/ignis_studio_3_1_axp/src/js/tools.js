function remove_diacritics(text)
{
    var sdiak="áäčďéěíĺľňóôőöŕšťúůűüýřžÁÄČĎÉĚÍĹĽŇÓÔŐÖŔŠŤÚŮŰÜÝŘŽ";
    var bdiak="aacdeeillnoooorstuuuuyrzAACDEEILLNOOOORSTUUUUYRZ";
    
    var tx = '';

    for(var p = 0; p < text.length; p++)
    { 
        if (sdiak.indexOf(text.charAt(p)) != -1)
        {
            tx+=bdiak.charAt(sdiak.indexOf(text.charAt(p)));
        } else {
            tx+=text.charAt(p);
        }
    }

    return tx;
}

function _base64ToArrayBuffer(base64) {
    var binary_string = window.atob(base64);
    var len = binary_string.length;
    var bytes = new Uint8Array(len);
    for (var i = 0; i < len; i++) {
        bytes[i] = binary_string.charCodeAt(i);
    }
    return bytes.buffer;
}
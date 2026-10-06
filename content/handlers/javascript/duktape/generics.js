/*
 * Generics for Duktape binding in NetSurf
 *
 * The result of this *MUST* be setting a NetSurf object only.
 *
 * That object will then be absorbed into the global object as a hidden
 * object which is used by the rest of the bindings.
 */

var NetSurf = {
    /* The make-proxy call for list-type objects */
    makeListProxy: function(inner) {
	return new Proxy(inner, {
	    has: function(target, key) {
		if (typeof key == 'number' || (typeof key == 'string' && /^(0|[1-9][0-9]*)$/.test(key))) {
		    return (key >= 0) && (key < target.length);
		} else {
		    return key in target;
		}
	    },
	    get: function(target, key) {
		if (typeof key == 'number' || (typeof key == 'string' && /^(0|[1-9][0-9]*)$/.test(key))) {
		    return key < target.length ? target.item(Number(key)) : undefined;
		} else {
		    return target[key];
		}
	    },
	});
    },
    /* The make-proxy call for nodemap-type objects */
    makeNodeMapProxy: function(inner) {
	return new Proxy(inner, {
	    has: function(target, key) {
		if (typeof key == 'number' || (typeof key == 'string' && /^(0|[1-9][0-9]*)$/.test(key))) {
		    return (key >= 0) && (key < target.length);
		} else {
		    return target.getNamedItem(key) || (key in target);
		}
	    },
	    get: function(target, key) {
		if (typeof key == 'number' || (typeof key == 'string' && /^(0|[1-9][0-9]*)$/.test(key))) {
		    return key < target.length ? target.item(Number(key)) : undefined;
		} else {
		    var attr = target.getNamedItem(key);
		    if (attr) {
			return attr;
		    }
		    return target[key];
		}
	    },
	});
    },
    consoleFormatter: function Formatter() {

	if (arguments.length == 0) {
	    return new Array("");
	} else if (arguments.length == 1) {
	    return new Array(arguments[0].toString());
	}

	const target = arguments[0];
	const current = arguments[1];

	if (typeof target !== "string") {
	    return Array.from(arguments);
	}

	const offset = target.search("%");

	if (offset == -1 || offset >= (target.length - 1)) {
	    // We've a string, but the % either doesn't exist or is
	    // at the end of it, so give up
	    return Array.from(arguments);
	}

	const specifier = target[offset + 1];

	var converted = undefined;

	if (specifier === 's') {
	    // Stringification
	    converted = current.toString();
	} else if (specifier === 'd' || specifier === 'i') {
	    converted = parseInt(current, 10).toString();
	} else if (specifier === 'f') {
	    converted = parseFloat(current).toString();
	} else if (specifier === 'o') {
	    // TODO: Objectification?
	    converted = current.toString();
	} else if (specifier === 'O') {
	    // TODO: JSONification
	    converted = current.toString();
	}

	var result = new Array();

	if (converted !== undefined) {
	    // We converted it, so we need to absorb the formatted thing
	    // and move on
	    var newtarget = "";
	    if (offset > 0) {
		newtarget = target.substring(0, offset);
	    }
	    newtarget = newtarget + converted;
	    if (offset < target.length - 2) {
		newtarget = newtarget + target.substring(offset + 2, target.length);
	    }
	    result.push(newtarget);
	} else {
	    // Undefined, so we drop this argument and move on
	    result.push(target);
	}

	var i;
	for (i = 2; i < arguments.length; i++) {
	    result.push(arguments[i]);
	}

	if (result[0].search("%") == -1) {
	    return result;
	}

	if (result.length === 1) {
	    return result;
	}

	return Formatter.apply(result);
    }
};

/* Only expose property aliases which the renderer implements. Methods read
 * cssText each time so changes made through setAttribute remain visible. */
NetSurf.makeStyleProxy = function (inner) {
    var properties = ('display visibility width height min-width min-height ' +
        'max-width max-height position top right bottom left color background ' +
        'background-color background-image margin margin-top margin-right ' +
        'margin-bottom margin-left padding padding-top padding-right ' +
        'padding-bottom padding-left border border-width border-style ' +
        'border-color font font-size font-family font-weight font-style ' +
        'line-height text-align text-decoration white-space overflow ' +
        'overflow-x overflow-y float clear opacity z-index').split(' ');
    var aliases = Object.create(null);
    properties.forEach(function (name) {
        aliases[name.replace(/-([a-z])/g, function (_, c) {
            return c.toUpperCase();
        })] = name;
        aliases[name] = name;
    });
    aliases.cssFloat = 'float';
    function declarations() {
        var text = inner.cssText, result = [], start = 0, quote = '', depth = 0;
        /* Semicolons inside URLs, strings and functions are not separators. */
        for (var i = 0; i <= text.length; i++) {
            var c = text.charAt(i);
            if (c === '\\') { i++; continue; }
            if (quote) { if (c === quote) quote = ''; continue; }
            if (c === '"' || c === "'") { quote = c; continue; }
            if (c === '/' && text.charAt(i + 1) === '*') {
                var end = text.indexOf('*/', i + 2);
                if (end < 0) break;
                text = text.slice(0, i) + text.slice(end + 2); i--; continue;
            }
            if (c === '(') depth++;
            if (c === ')') depth--;
            if ((c === ';' && depth === 0) || i === text.length) {
                var declaration = text.slice(start, i), colon = declaration.indexOf(':');
                if (colon > 0) {
                    var name = declaration.slice(0, colon).trim().toLowerCase();
                    var value = declaration.slice(colon + 1).trim();
                    var important = /\s*!important\s*$/i.test(value);
                    if (value) result.push({name: name,
                        value: value.replace(/\s*!important\s*$/i, '').trim(),
                        priority: important ? 'important' : ''});
                }
                start = i + 1;
            }
        }
        return result;
    }
    function property(name) {
        var list = declarations(), found = {value: '', priority: ''};
        for (var i = 0; i < list.length; i++) {
            if (list[i].name === name &&
                (found.priority !== 'important' || list[i].priority === 'important'))
                found = list[i];
        }
        return found;
    }
    inner.getPropertyValue = function (name) { return property(String(name)).value; };
    inner.getPropertyPriority = function (name) { return property(String(name)).priority; };
    inner.setProperty = function (name, value, priority) {
        name = String(name); value = value == null ? '' : String(value);
        priority = priority == null ? '' : String(priority).toLowerCase();
        if (priority && priority !== 'important') return;
        if (properties.indexOf(name) < 0) return;
        var list = declarations().filter(function (p) { return p.name !== name; });
        if (value) list.push({name: name, value: value, priority: priority});
        inner.cssText = list.map(function (p) {
            return p.name + ': ' + p.value + (p.priority ? ' !important' : '') + ';';
        }).join(' ');
    };
    inner.removeProperty = function (name) {
        var previous = inner.getPropertyValue(name);
        inner.setProperty(name, '');
        return previous;
    };
    return new Proxy(inner, {
        has: function (target, key) { return key in aliases || key in target; },
        get: function (target, key) {
            return key in aliases ? target.getPropertyValue(aliases[key]) : target[key];
        },
        set: function (target, key, value) {
            if (key in aliases) target.setProperty(aliases[key], value);
            else target[key] = value;
            return true;
        }
    });
};

/* data-* values stay in libdom, so dataset and attribute writes share storage. */
NetSurf.makeDatasetProxy = function (inner, element) {
    function attribute(key) {
        return 'data-' + key.replace(/[A-Z]/g, function (c) {
            return '-' + c.toLowerCase();
        });
    }
    return new Proxy(inner, {
        get: function (target, key) {
            if (typeof key === 'number') key = String(key);
            if (typeof key !== 'string' || /-[a-z]/.test(key)) return target[key];
            var value = element.getAttribute(attribute(key));
            return value === null ? target[key] : value;
        },
        has: function (target, key) {
            if (typeof key === 'number') key = String(key);
            if (typeof key === 'string' && !/-[a-z]/.test(key) &&
                element.hasAttribute(attribute(key))) return true;
            return !Object.prototype.hasOwnProperty.call(target, key) && key in target;
        },
        set: function (target, key, value) {
            key = String(key);
            if (/-[a-z]/.test(key)) {
                var error = new Error('Dataset names cannot contain a hyphen followed by lowercase');
                error.name = 'SyntaxError';
                throw error;
            }
            element.setAttribute(attribute(key), String(value));
            return true;
        },
        deleteProperty: function (target, key) {
            element.removeAttribute(attribute(String(key)));
            delete target[key];
            return true;
        },
        ownKeys: function (target) {
            Object.keys(target).forEach(function (key) { delete target[key]; });
            var attributes = element.attributes, keys = [];
            for (var i = 0; i < attributes.length; i++) {
                var name = attributes.item(i).nodeName;
                if (name.slice(0, 5) !== 'data-' || /[A-Z]/.test(name)) continue;
                var key = name.slice(5).replace(/-([a-z])/g, function (_, c) {
                    return c.toUpperCase();
                });
                /* Duktape checks enumeration against the target's descriptors. */
                Object.defineProperty(target, key, {
                    value: undefined, enumerable: true, configurable: true, writable: true
                });
                keys.push(key);
            }
            return keys;
        }
    });
};

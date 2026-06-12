package sites

import "testing"

func TestValidateSiteNameReservedAndSlug(t *testing.T) {
	t.Parallel()
	tests := []struct {
		name string
		ok   bool
	}{
		{"demo", true},
		{"demo-1", true},
		{"api", false},
		{"www", false},
		{"Admin", false},
		{"-bad", false},
		{"bad-", false},
		{"bad_name", false},
		{"_quick", false},
		{"_private", false},
		{"_doctor-abc123", true},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			err := ValidateSiteName(tt.name, []string{"api", "admin", "www", "_quick"})
			if (err == nil) != tt.ok {
				t.Fatalf("ValidateSiteName(%q) err=%v ok=%v", tt.name, err, tt.ok)
			}
		})
	}
}

func TestSplitPathFallback(t *testing.T) {
	site, p, ok := SplitPathFallback("/~/demo/assets/app.js")
	if !ok || site != "demo" || p != "/assets/app.js" {
		t.Fatalf("unexpected split: site=%q p=%q ok=%v", site, p, ok)
	}
}

plugins { id("com.android.application"); id("org.jetbrains.kotlin.android"); id("org.jetbrains.kotlin.plugin.serialization") }
android {
    namespace = "org.astroattitude.example"
    compileSdk = 36
    defaultConfig { applicationId = "org.astroattitude.example"; minSdk = 26; targetSdk = 36; versionCode = 1; versionName = "0.1.0" }
    buildTypes { release { isMinifyEnabled = true; signingConfig = signingConfigs.getByName("debug"); proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt")) } }
    sourceSets["main"].assets.srcDir("../../../build/fixtures")
    compileOptions { sourceCompatibility = JavaVersion.VERSION_17; targetCompatibility = JavaVersion.VERSION_17 }
    kotlinOptions { jvmTarget = "17" }
}
dependencies { implementation(project(":library")); implementation("org.jetbrains.kotlinx:kotlinx-serialization-json:1.9.0") }

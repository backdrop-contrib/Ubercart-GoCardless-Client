CONTENTS OF THIS FILE
---------------------
   
 * Introduction
 * Requirements
 * Product payment types
 * International payments
 * Installation
 * Configuration
 * Recommended modules
 * Issues
 * FAQ
 * Maintainers


INTRODUCTION
------------
This module is provided by Seamless-CMS which is a partner of GoCardless.

It integrates the Ubercart e-commerce suite with the GoCardless online payment
service. Sites that implement the module are 'clients' of 
https://seamless-cms.co.uk, which generates GoCardless payments and debit
mandates on behalf of the client site.

Ubercart GoCardless Client enables customers to checkout and make Instant 
payments using open banking protocol, and/or create debit mandates for generating recurring payments.

The big advantage with GoCardless is that it is generally cheaper than
credit/debit card payment services. Fees in the UK are 1% + 20p, compared
with Paypal for example which is 3.4% + a flat fee. 

There are two recurring payment types available with GoCardless: Subscriptions
and One-off Payments. 

Subscriptions are automatic recurring payment, and work well for users
that want to take the same payment on a regular basis (for instance £5 per 
week, or £20 on the first of each month).

One-off Payments allow you, the end user, to trigger a payment against a 
debit mandate at any time with the API. This allows you to charge 
your end customers ad-hoc amounts.

 * For a full description of the module, visit the project page:
   https://drupal.org/project/uc_gc_client

 * To submit bug reports and feature suggestions, or to track changes:
   https://drupal.org/project/issues/uc_gc_client


REQUIREMENTS
------------

 * An SSL certificate (https) is required in order to use the module.

 * This module requires the following modules:

   - Ubercart Payment (https://www.drupal.org/project/ubercart)
   - Ubercart Product Attibutes (https://www.drupal.org/project/ubercart)
   - Date (https://www.drupal.org/project/date)


PRODUCT PAYMENT TYPES
---------------------
Each product in your store is individually configured to use either Instant
payments, or one of the two recurring payment types: One-off payments, or 
Subscription payments.

Instant payments are single payments created upon checkout by GoCardless using
open banking protocol and do not require a debit mandate. Open banking does not
require credit/debit cards but the experience is similar to other online
payment services in that the customer is required to provide their bank
details, and to verify the bank account.

Subscription and One-off payments both require a direct debit mandate, which is
created by GoCardless upon checkout. Subscriptions are recurring payments that
are created automatically by GoCardless according to the rules that you set for
the product. One-off payments are (recurring) payments that are created
automatically when your website instructs GoCardless to do so, and are more
flexible than Subscription payments. Debit payments take several days to
complete following an instruction from GoCardless to the bank to create the
payment.

Instant payments are only created if none of the products in a cart are using
recurrence rules, or if they are One-off payments that are configured to 
'Raise payment immediately' upon checkout. Instant payments are currenly only
available for customers with British or German bank accounts. If it is not
possible to create an Instant payment upon checkout, a debit mandate will be
created instead, and the product will be paid for with a One-off payment.


INSTALLATION
------------
 
 * Install as you would normally install a contributed Drupal module. Visit:
   https://drupal.org/documentation/install/modules-themes/modules-7
   for further information.


CONFIGURATION
-------------

 * Install and enable the Ubercart payment service in the normal way from
   the payment method settings page at:
   admin >> store >> configuration >> payment methods >> GoCardless. 

 * Before you can use your site with GoCardless you need to 'Connect' as a 
   client of Seamless CMS. Do this from the payment method setting page.
   Click 'Connect SANDBOX' and you will be taken through the GoCardless 
   "OAuth Flow". Upon return to the site you should be connected.
   You will be provided with a 30 byte Webhook Secret which you must set
   in your GoCardless account. You also need to go through the GoCardless
   'onboarding' process in order to verify your account and receive any
   payouts that you have generated.
   
 * Ensure that the Payment method pane is enabled at
   admin >> store >> settings >> checkout 

 * Additional settings are provided for each specific Ubercart product. These 
   are configured by clicking the GoCardless Settings vertical tab, at the
   bottom of the product's Edit form.

 * The module provides a Payment Interval attribute, with four presets: 
   weekly, fortnightly, monthly and yearly. Enabling this will allow your
   customers to choose the payment plan of their choice. Alternatively you can
   configure a product to a specific fixed payment interval.

 * Several hooks are provided to enable other modules to interact with this
   one at key moments, such as before setting up a new debit mandate,
   or before creating a payment, or after receiving a webhook from GoCardless.
   More information on using these is provided in uc_gc_client.api.php.


INTERNATIONAL PAYMENTS
----------------------

You can use GoCardless to create payments in a growing number of currencies.
To use international payments, you must contact help@gocardless.com and 
request that they enable the required region(s), so that you can use the
relevant debit schemes. Instructions on how to do this are at:
https://support.gocardless.com/hc/en-gb/articles/115002833785-Collecting-from-multiple-regions.

Alternatively you can request GoCardless to enable FX payment for your account,
which will enable you to collect payments in the whole range of GoCardless
currencies if you want.

Having done this, check "Create payments in foreign currencies" on the
GoCardless payment method settings page. 

After enabling foreign currencies, tell Ubercart which countries GoCardless
can use at: admin >> store >> configuration >> countries >> gocardless.
You should only enable countries here that use a currency that is enabled
for your GoCardless account (via FX payments or otherwise).

When checking out via GoCardless, the countries that are available for the 
billing and delivery addresses will be filterd to only include those selected
above. After redirection to GoCardless the customer is required to provide 
details of a bank that is located in the specified country, and if a debit
mandate is created it will use the relevant "scheme" (i.e. Bacs, Sepa, etc.). 

If the customer is paying with a foreign currency, then the module will obtain
the latest up to date, real exchange rates from the GoCardless API, and 
automatically alter the price for the product. For example a customer with a
French bank account purchases a £10 product from a British store and pays €12
after the price has been adjusted using the latest real exchange rates.

If you are using other payment methods in addition to GoCardless, you must 
enable and configure the uc_ajax module, to ensure that the correct countries 
are listed in the Delivery and Billing panes at checkout.

After enabling uc_ajax go to admin >> store >> settings >> checkout >> ajax:

  1. Add 'Payment method' as a Triggering form element 
  2. Select Delivery information and Billing information as Panes to update
  3. Submit 

You must also make sure that GC isn't the default payment method, because it
needs to be actively selected in checkout in order to filter the correct 
countries. (The default payment method is the first enabled method in the list
at admin >> store >> settings >> payment.)


RECOMMENDED MODULES
-------------------

 * Date Popup (https://www.drupal.org/project/date):
   This module ships with the Date module and when enabled it provides a
   user friendly popup widget for date fields.
 * Fieldset Helper (https://www.drupal.org/project/fieldset_helper):
   Provides enhanced user experience for customers and administrators
   by remembering the state of a Drupal collapsible fieldsets.


ISSUES
------

1. Connecting with GoCardless: There is a known issue with the cURL HTTP
   Request module, that causes authentication to fail when this module
   attempts to connect.

2. This module does not work if you are developing it on localhost. In order
   to for it to work you need a sandbox environment that is on the internet
   and has a domain name.


FAQ
---

Q: Where can I get an affordable SSL certificate for my site?

A: SSL certificates are available for free from https://www.sslforfree.com. 
   Let's Encrypt is the first free and open Certificate Authority. Since they
   are a charity it is recommended that you make a small donation to their
   service to help make it sustainable.

Q: Why not integrate directly with GoCardless rather than as a client of
   Seamless CMS?

A: It is perfectly possible to do this and GoCardless provide very good
   instructions for using their API. However, as a partner of GoCardless, 
   Seamless CMS generates an income of 10% of GoCardless' fees 
   (0.1% of each transaction). It is intended that by building up this 
   business, I can develop a modest income stream to ensure that the module
   is properly maintained, and I am able to respond efficiently to security
   threats, issues, and feature requests. So please help spread the word!


MAINTAINERS
-----------

Current maintainers:
 * Rob Squires (roblog) - https://www.drupal.org/u/roblog
